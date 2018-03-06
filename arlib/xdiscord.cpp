#ifdef ARLIB_SOCKET
#include "xdiscord.h"
#include <time.h>

#define USER_AGENT "DiscordBot (https://github.com/Alcaro/Arlib/blob/master/arlib/xdiscord.cpp, " __DATE__ " " __TIME__ ")"
void Discord::headers(array<string>& h)
{
	if (bot)
	{
		h.append("Authorization: Bot "+token);
		h.append("User-Agent: " USER_AGENT);
	}
	else
	{
		*(char*)0=0;
	}
}

void Discord::http(HTTP::req r, function<void(HTTP::rsp)> callback)
{
	if (ratelimit && ratelimit >= time(NULL))
	{
		HTTP::rsp fakersp;
		fakersp.success = false;
		fakersp.status = 429;
		callback(std::move(fakersp));
		return;
	}
	else ratelimit = 0;
	
	r.url = "https://discordapp.com/api" + r.url;
	headers(r.headers);
	//if (r.method == "POST" && !r.postdata) r.postdata = "{}";
	puts("request "+r.url);
	//r.userdata = ++m_http_index;
	m_http.send(r, callback);
}

//void Discord::http_process()
//{
////if(m_http_reqs)puts("HTTPACTIVE:"+tostring(m_http_reqs.size()));
//	//if (!m_http_reqs) return;
//	if (m_http.ready())
//	{
//		HTTP::rsp r = m_http.recv();
//puts("remove this once rate limit behavior has been found");
//puts(tostring(r.status));
//for(string& h : r.headers)puts(h);
//puts(r.text().c_str());
//		if (r.status == 429)
//		{
//			size_t timer;
//			fromstring(r.header("Retry-After"), timer);
//			if (timer < 1000) timer = 1000;
//			ratelimit = time(NULL) + (timer+999)/1000;
//		}
//		
//		function<void(HTTP::rsp)> callback = m_http_callbacks.get_or(r.q.userdata, NULL);
//		m_http_callbacks.remove(r.q.userdata);
//		if (callback) callback(std::move(r));
//	}
//}

void Discord::connect_bot(cstring token)
{
	this->bot = true;
	this->token = token;
	connect();
}

void Discord::connect()
{
puts("DOCONNECT");
debug_connect = getdate();
if (debug_target) debug_target.message("Connecting at "+debug_connect);
	//use this as a garbage collector, kinda shitty but I'm too lazy to figure out where timeouts should be enforced
	m_http.reset();
	
	connecting = true;
	keepalive_sent = false;
	
	update_keepalive_cb();
	
	if (bot) http(HTTP::req("/gateway/bot"), bind_this(&Discord::connect_cb));
	else *(char*)0=0;
}

void Discord::connect_cb(HTTP::rsp r)
{
	connecting = false;
	
	guilds_to_join = -1;
	puts(r.text().c_str());
	
	string ws_url = JSON(r.text())["url"];
	if (!ws_url) return;
	ws_url += "?v=6&encoding=json";
puts("DOCONNECT:"+ws_url);
	array<string> heads;
	headers(heads);
	m_ws.callback(bind_this(&Discord::ws_str), NULL); // ignore errors, keepalive will detect that
	m_ws.connect(ws_url, heads);
}

bool Discord::keepalive_cb()
{
	// if we don't connect properly, lie to ourselves until we ping out (easiest way to say 'reconnect in a minute')
	if (guilds_to_join == 0)
	{
		JSON json;
		json["op"] = 1;
		json["d"] = sequence;
		send_ws(json);
	}
	else if (debug_target)
	{
		debug_target.message("con="+debug_connect+" now="+getdate()+"; pingsent="+tostring(keepalive_sent)+
		                     " int="+tostring(keepalive_ms)+"; gtj="+tostring(guilds_to_join));
	}
	
	if (keepalive_sent)
	{
		connect();
		return true;
	}
	keepalive_sent = true;
	return true;
}

void Discord::ws_str(string msg)
{
//printf("d=%i dn=%li t=%li\n",(bool)debug_target,debug_next,time(NULL));
//if (debug_target && time(NULL) >= debug_next)
//{
//	debug_next = time(NULL)+60;
//	debug_target.message("con="+debug_connect+"; ping@"+tostring(keepalive_next - time(NULL))+
//	                     "; sent="+tostring(keepalive_sent)+"; gtj="+tostring(guilds_to_join));
//}
if (guilds.contains("") || guilds.contains("0"))
{
debug_target.message("ERROR: In anonymous guild");
guilds.remove("");
guilds.remove("0");
}
	//if (msg.length() > 1000)
	//{
	//	datelog(">> "+msg.csubstr(0,600)+"...");
	//}
	//else
	{
		datelog(">> "+msg);
	}
	
	JSON json(msg);
	if (!json) abort();
	if (json["op"]==0) // Dispatch
	{
		sequence = json["s"];
		if (json["t"] == "READY")
		{
			my_user = json["d"]["user"]["id"];
			for (JSON& g : json["d"]["guilds"].list())
			{
				guilds.get_create(g["id"]);
puts("GUILDCREATE="+g["id"].str());
			}
			guilds_to_join = guilds.size();
puts("GTJ="+tostring(guilds_to_join));
for (auto& g : guilds)
{
puts("GUILD="+g.key);
}
			//resume = json["d"]["session_id"];
		}
		if (json["t"] == "GUILD_CREATE")
		{
			cstring id = json["d"]["id"];
			i_guild& g = guilds[id];
			g.name = json["d"]["name"];
			for (JSON& member_j : json["d"]["members"].list())
			{
				set_user(id, member_j);
			}
			for (JSON& chan_j : json["d"]["channels"].list())
			{
				cstring cid = chan_j["id"];
				i_channel& c = channels.get_create(chan_j["id"]);
				g.channels.add(cid);
				c.guild = id;
				c.name = chan_j["name"];
				//c.topic = chan_j["topic"]; // not needed
			}
			for (JSON& role_j : json["d"]["roles"].list())
			{
				cstring rid = role_j["id"];
				i_role& r = roles.get_create(rid);
				g.roles.add(rid);
				
				r.guild = id;
				r.name = role_j["name"];
			}
			
			if (json["d"]["large"])
			{
				JSON reqfull;
				reqfull["op"] = 8; // Request Guild Members
				reqfull["d"]["guild_id"] = id;
				reqfull["d"]["query"] = "";
				reqfull["d"]["limit"] = 0;
				reqfull["d"]["why-do-i-need-this"] =
					"workaround for what appears to be a bug on your end, "
					"sometimes I get ID-only user objects without having seen that user anywhere else; "
					"are events disappearing?";
				send_ws(reqfull);
			}
			
			guilds_to_join--;
			cb->on_connect(Guild(this, id), User(this, my_user, id));
puts("GTJ="+tostring(guilds_to_join));
		}
		if (json["t"] == "GUILD_MEMBERS_CHUNK")
		{
			cstring id = json["d"]["guild_id"];
			for (JSON& member_j : json["d"]["members"].list())
			{
				set_user(id, member_j);
			}
		}
		if (json["t"] == "CHANNEL_CREATE" || json["t"] == "CHANNEL_UPDATE")
		{
			if (json["d"]["type"] != 0) return;
			
			cstring cid = json["d"]["id"];
			cstring gid = json["d"]["guild_id"];
			
			i_channel& c = channels.get_create(cid);
			i_guild& g = guilds[gid];
			
			g.channels.add(cid);
			c.guild = gid;
			c.name = json["d"]["name"];
		}
		if (json["t"] == "GUILD_MEMBER_ADD")
		{
			cstring guild_id = json["d"]["guild_id"];
			set_user(guild_id, json["d"]);
			cb->on_guild_join(Guild(this, guild_id), User(this, json["d"]["user"]["id"], guild_id));
		}
		if (json["t"] == "GUILD_MEMBER_UPDATE" ||
		    json["t"] == "PRESENCE_UPDATE") // used for tag changes, don't like how it's documented partial/unreliable but apparently I need it
		{
			set_user(json["d"]["guild_id"], json["d"]);
		}
		if (json["t"] == "GUILD_MEMBER_REMOVE")
		{
			cstring guild_id = json["d"]["guild_id"];
			cstring user_id = json["d"]["user"]["id"];
			cb->on_guild_leave(Guild(this, guild_id), User(this, user_id, guild_id));
			del_user(guild_id, user_id);
		}
		if (json["t"] == "MESSAGE_CREATE")
		{
			string chan = json["d"]["channel_id"];
			string user = json["d"]["author"]["id"];
			string text = json["d"]["content"];
			string guild = channels.contains(chan) ? channels[chan].guild : "0";
			
			set_user(guild, json["d"]);
			
			if (user != my_user && users[user].discriminator) // ignore partial users, probably timing issue
			{
				cb->on_msg(Channel(this, chan), User(this, user, guild), text);
			}
		}
		//if (json["t"] == "RESUMED")
		//{
		//	guilds_to_join = 0;
		//}
	}
	//if (json["op"]==1) // Heartbeat (client only)
	//if (json["op"]==2) // Identify (client only)
	//if (json["op"]==3) // Status Update (client only)
	//if (json["op"]==4) // Voice State Update (voice unsupported)
	//if (json["op"]==5) // Voice Server Ping (voice unsupported)
	//if (json["op"]==6) // Resume (client only)
	if (json["op"]==7) // Reconnect
	{
		m_ws.reset();
	}
	//if (json["op"]==8) // Request Guild Members (client only)
	if (json["op"]==9) // Invalid Session
	{
		m_ws.reset();
	}
	if (json["op"]==10) // Hello
	{
		keepalive_ms = json["d"]["heartbeat_interval"];
		update_keepalive_cb();
		
		//if (resume && guilds_to_join == 0)
		//{
		//	JSON json;
		//	json["op"] = 6;
		//	json["d"]["token"] = token;
		//	json["d"]["session_id"] = resume;
		//	json["d"]["seq"] = sequence;
		//	send(json);
		//	resume = "";
		//}
		//else
		{
			JSON json;
			json["op"] = 2;
			json["d"]["token"] = token;
			json["d"]["compress"] = false;
			json["d"]["properties"]["os"] = "linux";
			json["d"]["properties"]["browser"] = "standalone";
			json["d"]["properties"]["device"] = USER_AGENT;
			json["d"]["properties"]["referrer"] = "";
			json["d"]["properties"]["referring_domain"] = "";
			json["d"]["large_threshold"] = 250;
			send_ws(json);
		}
	}
	if (json["op"]==11) // Heartbeat ACK
	{
		keepalive_sent = false;
	}
}

//takes a User object, with ["id"]
void Discord::set_user_inner(JSON& json)
{
	if (json["username"])
	{
		cstring uid = json["id"];
		i_user& user = users.get_create(uid);
		user.username = json["username"];
		user.discriminator = json["discriminator"];
	}
}

//takes a Guild Member object, with ["user"] and ["roles"]
void Discord::set_user(cstring guild_id, JSON& json)
{
	set_user_inner(json["user"]);
	
	if (guild_id == "0") return;
	
	cstring uid = json["user"]["id"];
	i_user& user = users.get_create(uid);
	
	i_guild& guild = guilds[guild_id];
	guild.users.add(uid);
	user.nicks.insert(guild_id, json["nick"]);
	
	if (json["roles"])
	{
		for (cstring r : guild.roles)
		{
			user.roles.remove(r);
		}
		for (JSON& r : json["roles"].list())
		{
			user.roles.add(r.str());
		}
	}
	
	if (json["user"]["bot"]) user.is_bot = true;
}

void Discord::del_user(cstring guild_id, cstring user_id)
{
	i_guild& guild = guilds[guild_id];
	i_user& user = users[user_id];
	for (cstring role_id : guild.roles)
	{
		user.roles.remove(role_id);
	}
	guild.users.remove(user_id);
	for (auto& otherguild : guilds)
	{
		if (otherguild.value.users.contains(user_id)) return;
	}
	users.remove(user_id);
}

void Discord::User::fetch(function<void(User)> callback)
{
puts("PARTIAL:NAME="+impl().username);
puts("PARTIAL:PARTIAL="+tostring(partial()));
	if (!partial())
	{
puts("PARTIAL:SHORTCIRCUIT");
		callback(*this);
		return;
	}
puts("PARTIAL:FULLFETCH");
	
	class x {
		Discord* m_parent;
		function<void(User)> callback;
	public:
		x(Discord* m_parent, function<void(User)> callback) : m_parent(m_parent), callback(callback) {}
		void cb_fn(HTTP::rsp r)
		{
			JSON json(r.text());
			m_parent->set_user_inner(json);
			callback(m_parent->user_from_id(json["id"]));
			delete this;
		}
	};
	
	m_parent->http("GET", "/users/"+m_id, bind_ptr(&x::cb_fn, new x(m_parent, callback)));
}

string Discord::getdate()
{
	time_t rawtime;
	struct tm * timeinfo;
	time(&rawtime);
	timeinfo=localtime(&rawtime);
	char out[64];
	strftime(out, 64, "[%H:%M:%S]", timeinfo);
	return out;
}

void Discord::datelog(cstring text)
{
	puts(getdate()+" "+text);
}

void Discord::debug()
{
	for (auto& role : roles)
	{
		puts("role "+role.key+": "+role.value.name+" @ "+role.value.guild);
	}
	for (auto& channel : channels)
	{
		puts("chan "+channel.key+": "+channel.value.name+" @ "+channel.value.guild);
		for (cstring user : channel.value.users) puts("  member "+user);
	}
	for (auto& user : users)
	{
		puts("user "+user.key+": "+user.value.username+"#"+user.value.discriminator);
		for (auto& nick : user.value.nicks) puts("  nick @"+nick.key+" "+nick.value);
		for (cstring role : user.value.roles) puts("  role "+role);
	}
	for (auto& guild : guilds)
	{
		puts("guild "+guild.key+": "+guild.value.name);
		for (cstring user : guild.value.users) puts("  member "+user);
		for (cstring role : guild.value.roles) puts("  role "+role);
		for (cstring channel : guild.value.channels) puts("  channel "+channel);
	}
}
#endif
