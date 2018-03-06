//This is based on
// http://www.codeproject.com/Articles/136799/ Lightweight Generic C++ Callbacks (or, Yet Another Delegate Implementation)
//but heavily changed. The general idea remains, but the code is barely recognizable anymore. The changelog is too big to list.

//List of libraries that do roughly the same thing:
//http://www.codeproject.com/Articles/7150/ Member Function Pointers and the Fastest Possible C++ Delegates
// rejected because it uses ugly hacks which defeat the optimizer, unknown compilers, and my brain
//http://www.codeproject.com/Articles/11015/ The Impossibly Fast C++ Delegates
// rejected because creation syntax is ugly
//http://www.codeproject.com/Articles/13287/ Fast C++ Delegate
// rejected because it's huge and can allocate
//http://www.codeproject.com/Articles/18886/ A new way to implement Delegate in C++
// rejected because it depends on sizeof in creepy ways and can throw
//http://www.codeproject.com/Articles/136799/ Lightweight Generic C++ Callbacks (or, Yet Another Delegate Implementation)
// chosen because it gives the slimmest function objects - unlike the others, it's just two pointers

#ifndef UTIL_CALLBACK_HPP
#define UTIL_CALLBACK_HPP

#include <stddef.h>
#include <utility>

#define UTIL_CALLBACK_HPP_INSIDE

#define bind_free(func) (GetFreeCallbackFactory(func).Bind<func>())
#define bind_ptr(func, ptr) (GetCallbackFactory(func).Bind<func>(ptr))
#define bind_this(func) bind_ptr(func, this) // reminder: bind_this(&classname::function), not bind_this(function)

#define bind_ptr_del(func, ptr) (GetCallbackFactory(func).BindDelete<func>(ptr))
#define bind_this_del(func) bind_ptr_del(func, this)
#define bind_ptr_del_exp(func, ptr, destructor) (GetCallbackFactory(func).BindDeleteExp<func, destructor>(ptr))
#define bind_this_del_exp(func, destructor) bind_ptr_del_exp(func, destructor, this)

template<typename FuncSignature> class function;

#define JOIN2(a,b) a##b
#define JOIN(a,b) JOIN2(a,b)

#define FreeCallbackFactory JOIN(FreeCallbackFactory,COUNT)
#define MemberCallbackFactory JOIN(MemberCallbackFactory,COUNT)
#define ConstMemberCallbackFactory JOIN(ConstMemberCallbackFactory,COUNT)
#define BoundCallbackFactory JOIN(BoundCallbackFactory,COUNT)

#define ARG_TYPES_I(n) JOIN(P,n)
#define ARG_TYPES LOOP(ARG_TYPES_I)
#if __cplusplus >= 201103L
#define ARG_NAMES_MOVE_I(n) std::move(JOIN(a,n))
#else
#define ARG_NAMES_MOVE_I(n) JOIN(a,n)
#endif
#define ARG_NAMES_MOVE LOOP(ARG_NAMES_MOVE_I)
#define ARG_TYPES_AND_NAMES_I(n) JOIN(P,n) JOIN(a,n)
#define ARG_TYPES_AND_NAMES LOOP(ARG_TYPES_AND_NAMES_I)
#define TYPENAMES_I(n) typename JOIN(P,n)
#define TYPENAMES LOOP(TYPENAMES_I)

#define TYPENAMES2_I(n) typename JOIN(FP,n)
#define TYPENAMES2 LOOP(TYPENAMES2_I)

//this gives some quite terrible error messages if I typo something and it prints 20 overloads of GetCallbackFactory
//perhaps C++11 parameter packs could help, but I can barely comprehend them 
#define COUNT 0
#define LOOP(macro) /* */
#define C /* */
#include "function.h"
#undef C

#define C ,
#define COUNT 1
#define LOOP(macro) macro(1)
#include "function.h"

#define COUNT 2
#define LOOP(macro) macro(1), macro(2)
#include "function.h"

#define COUNT 3
#define LOOP(macro) macro(1), macro(2), macro(3)
#include "function.h"

#define COUNT 4
#define LOOP(macro) macro(1), macro(2), macro(3), macro(4)
#include "function.h"

#define COUNT 5
#define LOOP(macro) macro(1), macro(2), macro(3), macro(4), macro(5)
#include "function.h"

#define COUNT 6
#define LOOP(macro) macro(1), macro(2), macro(3), macro(4), macro(5), macro(6)
#include "function.h"

#undef C
#undef JOIN2
#undef JOIN
#undef FreeCallbackFactory
#undef MemberCallbackFactory
#undef ConstMemberCallbackFactory
#undef BoundCallbackFactory
#undef ARG_TYPES_I
#undef ARG_TYPES
#undef ARG_NAMES_MOVE_I
#undef ARG_NAMES_MOVE
#undef ARG_TYPES_AND_NAMES_I
#undef ARG_TYPES_AND_NAMES
#undef TYPENAMES_I
#undef TYPENAMES
#undef TYPENAMES2_I
#undef TYPENAMES2

#undef UTIL_CALLBACK_HPP_INSIDE


#if __cplusplus >= 201103L
#include <type_traits>

template<typename Tl, typename Tptr>
class LambdaBinderP {
	Tl l;
	Tptr p;
public:
	LambdaBinderP(Tl l, Tptr p) : l(std::move(l)), p(p) {}
	
	//destructor usage not available if there's an explicit pointer
	template<typename Tf> operator function<Tf>()
	{
		typedef typename function<Tf>::template FuncTypeWith<Tptr>::type fpSrcT;
		typedef typename function<Tf>::FuncType                          fpDstT;
		
		fpSrcT fpSrc = l;
		fpDstT fpDst = (fpDstT)fpSrc;
		
		return function<Tf>(fpDst, p);
	}
};
template<typename Tl, typename Tptr> inline LambdaBinderP<Tl, Tptr> bind_lambda(Tl l, Tptr p)
{
	return LambdaBinderP<Tl, Tptr>(l, p);
}

template<typename Tl>
class LambdaBinder {
	Tl l;
	
	template<typename T> class holder;
	template<typename Ret, typename... Args>
	class holder<Ret(Args...)> {
		Tl l;
	public:
		holder(Tl l) : l(std::move(l)) {}
		
		static Ret
		call(const void * ph, Args... args)
		{
			holder* h = (holder*)ph;
			return h->l(std::forward<Args>(args)...);
		}
		
		static void destruct(const void * ph)
		{
			holder* h = (holder*)ph;
			delete h;
		}
	};
public:
	LambdaBinder(Tl l) : l(std::move(l)) {}
	
	template<typename Tf>
	typename std::enable_if< std::is_convertible<Tl, typename function<Tf>::FuncTypeNp>::value, function<Tf>>::type
	doIt()
	{
		typename function<Tf>::FuncTypeNp tmp = l;
		return function<Tf>(tmp);
	}
	
	template<typename Tf>
	typename std::enable_if<!std::is_convertible<Tl, typename function<Tf>::FuncTypeNp>::value, function<Tf>>::type
	doIt()
	{
		return function<Tf>(&holder<Tf>::call, &holder<Tf>::destruct, new holder<Tf>(l));
	}
	
	template<typename Tf> operator function<Tf>()
	{
		return doIt<Tf>();
	}
};
template<typename Tl> inline LambdaBinder<Tl> bind_lambda(Tl l)
{
	return LambdaBinder<Tl>(l);
}
#endif

#endif

#ifdef UTIL_CALLBACK_HPP_INSIDE
template<typename R C TYPENAMES>
class function<R (ARG_TYPES)>
{
    template<typename T> friend class function;
private:
    class null_only;

public:
    typedef R (*FuncType)(const void* C ARG_TYPES);
    typedef R (*FuncTypeNp)(ARG_TYPES);
    template<typename Tp> class FuncTypeWith {
    public:
        typedef R (*type)(Tp C ARG_TYPES);
    };

private:
    typedef void (*DestructorType)(const void*);

    struct refcount
    {
        size_t count;
        DestructorType destruct;
    };

    FuncType func;
    const void* obj;
    refcount* ref;

public:
    function(FuncType f, const void* o) : func(f), obj(o), ref(NULL) {}
    function(FuncType f, DestructorType d, const void* o) : func(f), obj(o)
    {
        ref = new refcount;
        ref->count = 1;
        ref->destruct = d;
    }

private:
    void unref()
    {
        if (ref)
        {
            ref->count--;
            if (ref->count == 0)
            {
                ref->destruct(obj);
                delete ref;
            }
        }
    }
    void add_ref()
    {
        if (ref) ref->count++;
    }

    function(FuncType f, const void* o, refcount* ref) : func(f), obj(o), ref(ref) { add_ref(); }

public:
    //to make null objects callable, 'func' must be a valid function
    //I can not:
    //- use the lowest bits - requires mask at call time, and confuses the optimizer
    //- compare it to a static null function, I don't trust the compiler to merge it correctly
    //- use a function defined in a .cpp file - this is a single-header library and I want it to remain that way
    //nor can I use NULL/whatever in 'obj', because foreign code can find that argument just as easily as this one can
    //solution: set obj=func=EmptyHandler for null functions
    //- EmptyHandler doesn't use obj, it can be whatever
    //- it is not sensitive to false negatives - even if the address of EmptyHandler changes, obj==func does not
    //- it is not sensitive to false positives - EmptyHandler is private, and can't be aliased by anything unexpected
    //- it is sensitive to hostile callers, but if you call bind_ptr(func, (void*)func), you're asking for bugs.
    function()                    : func(EmptyHandler), obj((void*)EmptyHandler), ref(NULL) {}
    function(const function& rhs) : func(rhs.func), obj(rhs.obj), ref(rhs.ref)
        { add_ref(); }
#if __cplusplus >= 201103L
    function(function&& rhs)      : func(rhs.func), obj(rhs.obj), ref(rhs.ref)
        { rhs.ref = NULL; } // elide poking the refcount if not needed
#endif

    function(nullptr_t)           : func(EmptyHandler), obj((void*)EmptyHandler), ref(NULL) {}

    function& operator=(const function& rhs)
        { unref(); obj = rhs.obj; func = rhs.func; ref = rhs.ref; add_ref(); return *this; }
#if __cplusplus >= 201103L
    function& operator=(function&& rhs)
        { unref(); obj = rhs.obj; func = rhs.func; ref = rhs.ref; rhs.ref = NULL; return *this; }
#endif
    ~function() { unref(); }

    inline R operator()(ARG_TYPES_AND_NAMES) const
    {
        //return value isn't moved, it'd error if void. but compiler is allowed to perform copy elision, and most likely does
        return (*func)(obj C ARG_NAMES_MOVE);
    }

private:
    typedef const void* function::*SafeBoolType;
    bool isTrue() const
    {
      return ((void*)func != obj);
    }
public:
    inline operator SafeBoolType() const
        { return isTrue() ? &function::obj : NULL; }
    inline bool operator!() const
        { return !isTrue(); }

private:

    static R EmptyHandler(const void* o C ARG_TYPES_AND_NAMES) { return R(); }

    template<typename FR C TYPENAMES2>
    friend class FreeCallbackFactory;
    template<typename FR, class FT C TYPENAMES2>
    friend class MemberCallbackFactory;
    template<typename FR, class FT C TYPENAMES2>
    friend class ConstMemberCallbackFactory;
    template<typename FR C TYPENAMES2, typename PTR>
    friend class BoundCallbackFactory;
    
private:
    static R FreeWrapper(const void* arg C ARG_TYPES_AND_NAMES)
    {
        FuncTypeNp func = (FuncTypeNp)arg;
        return func(ARG_NAMES_MOVE);
    }

    void set_to_free(FuncTypeNp func_raw)
    {
        if (func_raw)
        {
            func = FreeWrapper;
            obj = (void*)func_raw;
        }
        else
        {
            func = EmptyHandler;
            obj = (void*)EmptyHandler;
        }
        ref = NULL;
    }

    class PrivateType {};
public:
    //strange how operator= can deduce T without this default argument, but constructor can't
    //this shouldn't match if there's two constructor arguments, nothing is convertible to PrivateType
    template<typename T>
    function(T func_raw_ref,
             typename std::enable_if<std::is_convertible<T, FuncTypeNp>::value, PrivateType>::type ignore = PrivateType())
    {
        set_to_free(func_raw_ref);
    }
    template<typename T>
    function& operator=(typename std::enable_if<std::is_convertible<T, FuncTypeNp>::value, T>::type func_raw_ref)
    {
        unref();
        set_to_free(func_raw_ref);
    }

    //WARNING: Dangerous if mishandled! Ensure that every type (including return) is either unchanged,
    // or a primitive type (integer, float or pointer - no structs or funny stuff) of the same size as the original.
    //I'd stick in some static_assert to enforce that, but with the variable size of the argument lists,
    // that'd be annoying. Not sure how to unpack T, either.
    template<typename T>
    function<T> reinterpret()
    {
        //static_assert(std::is_fundamental<T>::value);
        //static_assert(std::is_fundamental<T2>::value);
        //static_assert(sizeof(T)==sizeof(T2));
        return function<T>((typename function<T>::FuncType)func, obj, (typename function<T>::refcount*)ref);
    }
};

template<typename R C TYPENAMES>
class FreeCallbackFactory
{
private:
    template<R (*Func)(ARG_TYPES)>
    static R Wrapper(const void* C ARG_TYPES_AND_NAMES)
    {
        return (*Func)(ARG_NAMES_MOVE);
    }

public:
    template<R (*Func)(ARG_TYPES)>
    inline static function<R (ARG_TYPES)> Bind()
    {
        return function<R (ARG_TYPES)>
            (&FreeCallbackFactory::Wrapper<Func>, NULL);
    }
};

template<typename R C TYPENAMES>
inline FreeCallbackFactory<R C ARG_TYPES>
GetFreeCallbackFactory(R (*)(ARG_TYPES))
{
    return FreeCallbackFactory<R C ARG_TYPES>();
}

template<typename R, class T C TYPENAMES>
class MemberCallbackFactory
{
private:
    template<R (T::*Func)(ARG_TYPES)>
    static R Wrapper(const void* o C ARG_TYPES_AND_NAMES)
    {
        T* obj = const_cast<T*>(static_cast<const T*>(o));
        return (obj->*Func)(ARG_NAMES_MOVE);
    }
    
    static void DeleteWrapper(const void* o)
    {
        T* obj = const_cast<T*>(static_cast<const T*>(o));
        delete obj;
    }
    
    template<R (T::*Func)(ARG_TYPES)>
    static void DestructorWrapper(const void* o)
    {
        T* obj = const_cast<T*>(static_cast<const T*>(o));
        return (obj->*Func)();
    }

public:
    template<R (T::*Func)(ARG_TYPES)>
    static inline function<R (ARG_TYPES)> Bind(T* o)
    {
        return function<R (ARG_TYPES)>
            (&MemberCallbackFactory::Wrapper<Func>,
            static_cast<const void*>(o));
    }
    
    template<R (T::*Func)(ARG_TYPES)>
    static inline function<R (ARG_TYPES)> BindDelete(T* o)
    {
        return function<R (ARG_TYPES)>
            (&MemberCallbackFactory::Wrapper<Func>,
             &MemberCallbackFactory::DeleteWrapper,
             static_cast<const void*>(o));
    }
    
    template<R (T::*Func)(ARG_TYPES), void (T::*Destructor)()>
    static inline function<R (ARG_TYPES)> BindDeleteExp(T* o)
    {
        return function<R (ARG_TYPES)>
            (&MemberCallbackFactory::Wrapper<Func>,
             &MemberCallbackFactory::DestructorWrapper<Destructor>,
             static_cast<const void*>(o));
    }
};

template<typename R, class T C TYPENAMES>
inline MemberCallbackFactory<R, T C ARG_TYPES>
GetCallbackFactory(R (T::*)(ARG_TYPES))
{
    return MemberCallbackFactory<R, T C ARG_TYPES>();
}

template<typename R, class T C TYPENAMES>
class ConstMemberCallbackFactory
{
private:
    template<R (T::*Func)(ARG_TYPES) const>
    static R Wrapper(const void* o C ARG_TYPES_AND_NAMES)
    {
        const T* obj = static_cast<const T*>(o);
        return (obj->*Func)(ARG_NAMES_MOVE);
    }
    
    template<void (T::*Func)() const>
    static void DestructorWrapper(const void* o)
    {
        const T* obj = static_cast<const T*>(o);
        return (obj->*Func)();
    }

public:
    template<R (T::*Func)(ARG_TYPES) const>
    static inline function<R (ARG_TYPES)> Bind(const T* o)
    {
        return function<R (ARG_TYPES)>
            (&ConstMemberCallbackFactory::Wrapper<Func>,
            static_cast<const void*>(o));
    }
    //no BindDelete, it's for non-const objects only
    template<R (T::*Func)(ARG_TYPES) const, void (T::*Destructor)()>
    static inline function<R (ARG_TYPES)> BindDeleteExp(const T* o)
    {
        return function<R (ARG_TYPES)>
            (&ConstMemberCallbackFactory::Wrapper<Func>,
             &ConstMemberCallbackFactory::DestructorWrapper<Destructor>,
             static_cast<const void*>(o));
    }
};

template<typename R, class T C TYPENAMES>
inline ConstMemberCallbackFactory<R, T C ARG_TYPES>
GetCallbackFactory(R (T::*)(ARG_TYPES) const)
{
    return ConstMemberCallbackFactory<R, T C ARG_TYPES>();
}

template<typename R C TYPENAMES, typename PTR>
class BoundCallbackFactory
{
private:
    template<R (*Func)(PTR* C ARG_TYPES)>
    static R Wrapper(const void* o C ARG_TYPES_AND_NAMES)
    {
        return (*Func)((PTR*)o C ARG_NAMES_MOVE);
    }
    template<void (*Func)(PTR*)>
    static R DestructorWrapper(const void* o)
    {
        return (*Func)((PTR*)o);
    }

public:
    template<R (*Func)(PTR* C ARG_TYPES)>
    static inline function<R (ARG_TYPES)> Bind(PTR* o)
    {
        return function<R (ARG_TYPES)>
            (&BoundCallbackFactory::Wrapper<Func>, o);
    }
    //no BindDelete, it's for objects only
    template<R (*Func)(PTR* C ARG_TYPES), void (*Destructor)(PTR*)>
    static inline function<R (ARG_TYPES)> BindDeleteExp(PTR* o)
    {
        return function<R (ARG_TYPES)>
            (&BoundCallbackFactory::Wrapper<Func>, &BoundCallbackFactory::DestructorWrapper<Destructor>, o);
    }
};

template<typename R C TYPENAMES, typename PTR>
inline BoundCallbackFactory<R C ARG_TYPES, PTR>
GetCallbackFactory(R (*)(PTR* C ARG_TYPES))
{
    return BoundCallbackFactory<R C ARG_TYPES, PTR>();
}

#undef COUNT
#undef LOOP
#endif
