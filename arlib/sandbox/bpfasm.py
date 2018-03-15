#!/usr/bin/env python3
#probably works on Python 2 too, but untested

#I don't really like bringing in Python, but the alternative is trying to express this in C++ preprocessor,
#which would be ugly both in use (LDA(I_arch) JNE(ARCH_TRUE, die) LABEL(die))
#and implementation (there's no obvious way to represent those labels)
#so this is the best I can do
#this is linux-only anyways, every linux modern enough to support execveat has python 3

#this mostly follows the assembly language specified in
#  https://www.kernel.org/doc/Documentation/networking/filter.txt
#with a few exceptions:
#  this is an optimizing assembler
#    most prominently, je followed by jmp is merged
#      as such, addressing mode 7 (combined je/jne) is not implemented; it's harder to read
#        instead, I added a jnset opcode
#    (TODO) large sequences of 'je' comparing to integer constants are turned into binary trees
#    additionally, the what-if rule is applied liberally
#  je out-of-bounds throws errors rather than truncating (TODO: convert to jne+jmp)
#  supports a primitive preprocessor: 'let __NR_open = 0' defines a named constant
#    you can also use 'defines /usr/include/x86_64-linux-gnu/asm/unistd_64.h' to read #defines from that file
#      (other preprocessor directives ignored)
#    unknown constants are passed to the C compiler, so you can #include the file there if you prefer
#      but that inhibits the je-ladder optimization
#  /* */ comments not supported; # (start of line only) and ; (anywhere) are
#seccomp-specific docs: https://www.kernel.org/doc/Documentation/prctl/seccomp_filter.txt

#upstream assembler source code:
# http://lxr.free-electrons.com/source/tools/net/bpf_exp.y

#output usage:
#	static const struct sock_filter filter[] = {
#		#include "bpf.inc"
#	};
#	static const struct sock_fprog prog = {
#		.len = (unsigned short)(sizeof(filter)/sizeof(filter[0])),
#		.filter = filter,
#	};
#	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0,0,0)!=0 || prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)!=0)
#		exit(1);

def assemble(bpf):
	def die(why):
		exit(why)
	
	#"pass" 1: compile list of opcodes and addressing modes
	#output: ops = { "ld": { "M[_]": "BPF_LD|BPF_W|BPF_MEM" } }
	import re
	addrmodes = [
		('-', "", ""),
		('0', "x", "|BPF_X"),
		('0', "%x", "|BPF_X"),
		('1', "[_]", "|BPF_ABS"),
		('2', "[x+_]", "|BPF_IND"),
		('3', "M[_]", "|BPF_MEM"), # BPF_MEM is documented as scratch space, probably useless for seccomp
		('E', "M[_]", ""), # turns out BPF_ST doesn't use BPF_MEM; E is almost-3
		('4', "#_", "|BPF_IMM"),
		('5', "4*([_]&0xf)", "|BPF_LEN"),
		# 6 is label, it's hardcoded
		# 7 is merged compare/true/false, not implemented
		# 8 is compare/fallthrough, also hardcoded
		('9', "a", "|BPF_A"),
		('9', "%a", "|BPF_A"),
		# 10 is various pseudoconstants like packet length, not available for BPF
		]
	ops = {}
	for op,val,modes in [
		("ld",  "BPF_LD|BPF_W", "1234"),
		("ldi", "BPF_LD|BPF_W", "4"), # the linux bpf assembler allows this without #, as an undocumented extension; I don't
		("ldh", "BPF_LD|BPF_H", "12"),
		("ldb", "BPF_LD|BPF_B", "12"),
		("ldx",  "BPF_LDX|BPF_W", "345"),
		("ldxi", "BPF_LDX|BPF_W", "4"),
		("ldxb", "BPF_LDX|BPF_B", "5"),
		("st",  "BPF_ST", "E"),
		("stx", "BPF_STX", "E"),
		("jmp", "BPF_JMP|BPF_JA",  None), # for branches, the third param is whether the label goes to True or False
		("jeq", "BPF_JMP|BPF_JEQ", True), # the other falls through
		("jne", "BPF_JMP|BPF_JEQ", False), # JMP goes to both, so None
		("jlt", "BPF_JMP|BPF_JGE", False),
		("jle", "BPF_JMP|BPF_JGT", False),
		("jgt", "BPF_JMP|BPF_JGT", True),
		("jge", "BPF_JMP|BPF_JGE", True),
		("jset",  "BPF_JMP|BPF_JSET", True),
		("jnset", "BPF_JMP|BPF_JSET", False),
		("add", "BPF_ALU|BPF_ADD", "04"),
		("sub", "BPF_ALU|BPF_SUB", "04"),
		("mul", "BPF_ALU|BPF_MUL", "04"),
		("div", "BPF_ALU|BPF_DIV", "04"),
		("mod", "BPF_ALU|BPF_MOD", "04"),
		("neg", "BPF_ALU|BPF_NEG", "-"),
		("and", "BPF_ALU|BPF_AND", "04"),
		("or",  "BPF_ALU|BPF_OR",  "04"),
		("xor", "BPF_ALU|BPF_XOR", "04"),
		("lsh", "BPF_ALU|BPF_LSH", "04"),
		("rsh", "BPF_ALU|BPF_RSH", "04"),
		("tax", "BPF_MISC|BPF_TAX", "-"),
		("txa", "BPF_MISC|BPF_TXA", "-"),
		("ret", "BPF_RET", "049"), # docs don't offer 'ret x', but assembler implements it; I'll trust assembler
		]:
		modesup = {}
		ops[op] = modesup
		if isinstance(modes,str):
			for mode in modes:
				for mode2,pattern,add in addrmodes:
					if mode==mode2:
						modesup[pattern] = val+add
		elif modes is None:
			modesup["_"] = None
		else:
			modesup["#_"] = (val,modes)
			modesup["x,#_"] = (val+"|BPF_X",modes) # TODO: check if docs mention this
			modesup["%x,#_"] = (val+"|BPF_X",modes)
	
	#pass 2: statements to instructions
	#after this, opcodes is an array with the following structure:
	"""
	ret #0
	.l=set("#0"), .op="BPF_RET|BPF_IMM", .val="0", .jt=None, .jf=None
	every opcode has an implicit label named after its opcode ID
	
	ld #42
	.l=set("#0"), .op="BPF_LD|BPF_W|BPF_IMM", .val="42", .jt="#1", .jf=None
	opcodes other than return have .jt, jump if true, pointing to next opcode
	
	test123: ldh [I_arch]
	.l=set("#1", "test123"), .op="BPF_LD|BPF_H|BPF_K", .val="I_arch", .jt="#2", .jf=None
	
	jeq #__NR_exit, test123
	.l=set("#0"), .op="BPF_JMP|BPF_JEQ", .val="__NR_exit", .jt="test123", .jf="#1"
	branch opcodes have two jump targets
	
	jne #__NR_exit, test123
	.l=set("#0"), .op="BPF_JMP|BPF_JEQ", .val="__NR_exit", .jt="#1", .jf="test123"
	
	jmp test123
	.l=set("#0"), .op=None, .val="0", .jt="test123", .jf="test123"
	unconditional jmp is implemented as .jt == .jf; opcode is ignored (hardcoded later)
	"""
	
	class struct:
		def clone(self):
			ret = struct()
			ret.__dict__ = self.__dict__.copy()
			return ret
		def __repr__(self):
			return str(self.__dict__)
	def opcode(l, op, val, jt, jf=None):
		ret = struct()
		ret.l = l
		ret.op = op
		ret.val = val
		ret.jt = jt
		ret.jf = jf
		return ret
	
	opcodes = []
	labelshere = set()
	
	defines = {}
	defines_rec = None
	define_chars = "a-zA-Z0-9_"
	def set_define(word, val):
		if not re.fullmatch("["+define_chars+"]+", val): val = "("+val+")"
		defines[word] = val
		nonlocal defines_rec
		defines_rec = None
	
	def define_split(text):
		return re.findall("["+define_chars+"]+|[^"+define_chars+"]+", text)
	
	def flatten_define(text, defines, used):
		words = define_split(text)
		for i,word in enumerate(words):
			if word in defines and word not in used:
				used.add(word)
				words[i] = flatten_define(defines[word], defines, used)
				used.remove(word)
		return ''.join(words)
	
	def flatten_defines(defines):
		flat = {}
		
		for src in defines:
			flat[src] = flatten_define(defines[src], defines, {src})
		
		return flat
	
	def get_define(text):
		nonlocal defines_rec
		if not defines_rec:
			defines_rec = flatten_defines(defines)
		
		words = define_split(text)
		for i,word in enumerate(words):
			if word in defines_rec:
				words[i] = defines_rec[word]
		return ''.join(words)
	
	for line in bpf.split("\n"):
		line = line.split(";")[0].strip()
		m = re.match(r'([A-Za-z_][A-Za-z0-9_]*):(.*)$', line)
		if m: # I'd make this a while, but Python doesn't like while x=foo():.
			labelshere.add(m.group(1))
			line = m.group(2).strip()
		
		if not line or line[0] == '#': continue
		
		labelshere.add("#"+str(len(opcodes)))
		nextlabel = "#"+str(len(opcodes)+1)
		
		parts = line.split(None, 1)
		if len(parts)==1: parts+=[""]
		op,arg = parts
		
		if op not in ops:
			if op == "let":
				define,eq,value = arg.split(" ", 2)
				if eq != "=" or value is None:
					die("bad let: "+line)
				set_define(define, value)
				continue
			if op == "defines":
				for line in open(arg).read().split("\n"):
					if line.startswith("#define"):
						parts = line.split(None, 2)
						if len(parts) == 3: # ignore #define _STDIO_H
							set_define(parts[1], parts[2])
				continue
			die("unknown opcode: "+line)
		
		label = None
		if op[0]=='j':
			if op == "jmp":
				opcodes.append(opcode(l=labelshere, op=None, val="0", jt=arg, jf=arg))
				labelshere = set()
				continue
			
			arg,label = arg.rsplit(",",1)
			arg = arg.strip()
			label = label.strip()
		
		for pattern in ops[op]:
			match = None
			if '_' in pattern:
				left,right = pattern.split("_")
				if arg.startswith(left) and arg.endswith(right):
					if right: match = arg[len(left):-len(right)]
					else: match = arg[len(left):]
			elif arg==pattern:
				match = "0"
			if match:
				match = get_define(match)
				opspec = ops[op][pattern]
				if isinstance(opspec,tuple):
					if opspec[1]: opcodes.append(opcode(l=labelshere, op=opspec[0], val=match, jt=label, jf=nextlabel))
					else:         opcodes.append(opcode(l=labelshere, op=opspec[0], val=match, jt=nextlabel, jf=label))
				elif 'BPF_RET' in opspec:
					opcodes.append(opcode(l=labelshere, op=opspec, val=match, jt=None))
				else:
					opcodes.append(opcode(l=labelshere, op=opspec, val=match, jt=nextlabel))
				break
		else:
			die("unknown addressing mode: "+line)
		labelshere = set()
	
	
	#pass 2.5: check that all referenced labels exist and go forwards
	labels = set()
	for op in reversed(opcodes):
		for l in op.l:
			if l in labels:
				die("duplicate label: "+l)
			labels.add(l)
		if op.jt and op.jt not in labels: die("unknown or backwards branch: "+op.jt)
		if op.jf and op.jf not in labels: die("unknown or backwards branch: "+op.jf)
	
	
	#"pass" 3: optimize
	again = True
	#again = False # uncomment to disable this pass, for debugging
	while again:
		again = False
		
		labels_used = set()
		labels_used.add("#0") # the entry point is always reachable
		label_op = {} # opcode containing a label
		for pos,op in enumerate(opcodes):
			for l in op.l:
				label_op[l] = op
			if op.jt: labels_used.add(op.jt)
			if op.jf: labels_used.add(op.jf)
		
		def is_jmp(op):
			return op.jf
		def is_jmp_always(op):
			return op.jf and label_op[op.jt] is label_op[op.jf]
		
		delete = [False for _ in opcodes]
		for i in range(len(opcodes)):
			hasnext = (i+1 != len(opcodes))
			
			#label isn't used -> remove
			opcodes[i].l = set(l for l in opcodes[i].l if l in labels_used)
			
			#opcode has no labels -> unreachable -> delete
			if not opcodes[i].l:
				delete[i] = True
			
			#two consecutive identical returns -> flatten
			if hasnext and not opcodes[i].jt and not opcodes[i+1].jt and opcodes[i].val==opcodes[i+1].val:
				delete[i] = True
				opcodes[i+1].l |= opcodes[i].l
			
			#conditional jump to unconditional (including implicit) -> replace target
			if is_jmp(opcodes[i]):
				target1 = label_op[opcodes[i].jt]
				if is_jmp_always(target1):
					opcodes[i].jt = target1.jt
					again = True
				target2 = label_op[opcodes[i].jf]
				if is_jmp_always(target2):
					opcodes[i].jf = target2.jt
					again = True
			
			if is_jmp_always(opcodes[i]):
				#unconditional jump to return or unconditional jump -> flatten
				target = label_op[opcodes[i].jt]
				if is_jmp_always(target) or 'BPF_RET' in target.op:
					newop = target.clone()
					newop.l = opcodes[i].l
					opcodes[i] = newop
					again = True
				#unconditional jump to next -> delete
				if target is opcodes[i+1]:
					delete[i] = True
					opcodes[i+1].l |= opcodes[i].l
		
		oldlen = len(opcodes)
		opcodes = [op for op,dele in zip(opcodes,delete) if not dele]
		if len(opcodes) != oldlen:
			again = True
	
	#pass 4: generate code, or if branches go out of bounds, deoptimize by splitting out-of-bounds conditional jumps
	out = []
	again = True
	while again:
		labels = {}
		again = False
		out = []
		for op in reversed(opcodes): # since we have only forwards branches, assembling backwards is easier
			pos = len(out)
			for l in op.l: labels[l] = pos
			pos -= 1 # -1 because jmp(0) goes to the next opcode, not the same one again
			
			if op.jf:
				if op.jt==op.jf:
					line = "BPF_STMT(BPF_JMP|BPF_JA, "+str(pos - labels[op.jt])+"),\n"
				else:
					jt = pos - labels[op.jt]
					jf = pos - labels[op.jf]
					if jt>255 or jf>255:
						#TODO: fix
						#(official assembler seems to just truncate, it seems to be only intended for debugging?)
						die("jump out of bounds")
					line = "BPF_JUMP("+op.op+", (uint32_t)("+op.val+"), "+str(jt)+","+str(jf)+"),\n"
			else:
				line = "BPF_STMT("+op.op+", (uint32_t)("+op.val+")),\n"
			out.append(line)
	
	if len(out)>65535:
		die("can't fit "+str(len(out))+" instructions in struct sock_fprog")
	return "".join(reversed(out))


def testsuite(silent):
	def test(bpf, exp):
		act = assemble(bpf.replace(";", "\n"))
		act = act.strip().replace("\n", " ")
		if act==exp:
			if not silent:
				print("input:", bpf)
				print("pass")
				print()
		else:
			print("input:", bpf)
			print("expected:", exp)
			print("actual:  ", act)
			print()
			exit(1)
	#ensure this opcode assembles properly
	test("txa; ret #0", "BPF_STMT(BPF_MISC|BPF_TXA, (uint32_t)(0)), BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(0)),")
	#ensure labels don't need to be on their own line
	test("jmp x; x: ret #0", "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(0)),")
	#ensure let/defines assemble properly
	test("let answer = 42; ret #answer", "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(42)),")
	test("defines /usr/include/x86_64-linux-gnu/asm/unistd_64.h; "+
	     "defines /usr/include/x86_64-linux-gnu/bits/syscall.h; "+
	     "ret #SYS_exit",
	     "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(60)),")
	#make sure defines act like variables, not C macros
	test("let two = 1+1; ret #2*two", "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(2*(1+1))),")
	#make sure they don't recurse inappropriately
	test("let bar = offsetof(struct foo, bar); ret #bar", "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)((offsetof(struct foo, bar)))),")
	
	#ensure jeq+jmp is merged, and the jmp is killed as dead code
	test("jeq #0, ok; jmp die; ok: ret #0; die: ret #1",
	     "BPF_JUMP(BPF_JMP|BPF_JEQ, (uint32_t)(0), 0,1), BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(0)), BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(1)),")
	#ensure jump to return is optimized
	test("jmp die; ok: ret #0; die: ret #1",
	     "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(1)),")
	#ensure consecutive identical returns are optimized
	test("jeq #0, x; ret #1; x: ret #1",
	     "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(1)),")
	#ensure jeq to jmp is flattened
	#(testcase is full of random crap, to inhibit other optimizations)
	test("jeq #0, c; jeq #0, a; jmp b; c:; ret #2; a:; jmp a2; b:; jmp b2; a2:; ld [0]; ret #0; b2:; ld [1]; ret #1",
	     "BPF_JUMP(BPF_JMP|BPF_JEQ, (uint32_t)(0), 1,0), "+ # jeq #0, c
	     "BPF_JUMP(BPF_JMP|BPF_JEQ, (uint32_t)(0), 1,3), "+ # jeq #0, a; jmp b; a: jmp a2; b: jmp b2
	     "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(2)), "+      # c: ret #2
	     "BPF_STMT(BPF_LD|BPF_W|BPF_ABS, (uint32_t)(0)), "+ # a2: ld [0]
	     "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(0)), "+      # ret #0
	     "BPF_STMT(BPF_LD|BPF_W|BPF_ABS, (uint32_t)(1)), "+ # b2: ld [1]
	     "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(1)),")       # ret #1
	#not implemented
	##ensure identical branches are merged
	#test("jeq #0, a; ld [0]; ret #0; a: ld [0]; ret #0",
	#     "BPF_STMT(BPF_LD|BPF_W|BPF_ABS, (uint32_t)(0)), "+ # ld [0]
	#     "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(0)), "+      # ret #0
	#     "BPF_STMT(BPF_LD|BPF_W|BPF_ABS, (uint32_t)(1)), "+ # b2: ld [1]
	#     "BPF_STMT(BPF_RET|BPF_IMM, (uint32_t)(1)),")       # ret #1
#testsuite(False)
testsuite(True)


import os, sys

if len(sys.argv)==1:
	bpf = sys.stdin.read()
	outfile = lambda: sys.stdout
if len(sys.argv)==2:
	if sys.argv[1]=='--test':
		testsuite(False)
		exit(0)
	
	bpf = open(sys.argv[1], "rt").read()
	outfile = lambda: open(os.path.splitext(sys.argv[1])[0]+".inc", "wt") # lambda to ensure the file isn't created on failure
	sys.stdout
if len(sys.argv)==3:
	bpf = open(sys.argv[1], "rt").read()
	outfile = lambda: open(sys.argv[2], "wt")

bpfbin = assemble(bpf)

outfile = outfile()
outfile.write("/* Autogenerated, do not edit. All changes will be undone. */\n")
outfile.write(bpfbin)
