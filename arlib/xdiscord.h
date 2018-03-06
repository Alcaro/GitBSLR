#pragma once
#include "global.h"
#include "string.h"
#include "array.h"
#include "websocket.h"
#include "http.h"
#include "json.h"
#include "linq.h"

#define Discord Xdiscord
class Discord {
	struct i_role;
	struct i_user;
	struct i_channel;
	struct i_guild;
	
public:
	Discord(runloop* loop) : loop(loop), m_ws(loop), m_http(loop) {}
	~Discord() { loop->remove(keepalive_id); }
	
	struct msg {
		static string raw_escape(cstring in)
		{
			string out;
			for (size_t i=0;i<in.length();i++)
			{
				if (strchr("\\<_*~:", in[i])) out+='\\';
				out+=in[i];
			}
			return out;
		}
		static string raw_bold(     cstring in) { return "**"+in+"**"; }
		static string raw_italic(   cstring in) { return  "*"+in+"*"; }
		static string raw_underline(cstring in) { return "__"+in+"__"; }
		static string raw_strike(   cstring in) { return "~~"+in+"~~"; }
		
		
		string raw;
		msg() {}
		msg(string text) { raw = raw_escape(text); }
		msg(cstring text) { raw = raw_escape(text); }
		msg(const char * text) { raw = raw_escape(text); }
		
		static msg from_md(cstring in) { msg out; out.raw = in; return out; }
		
		msg bold()      { return from_md(     raw_bold(raw)); }
		msg italic()    { return from_md(   raw_italic(raw)); }
		msg underline() { return from_md(raw_underline(raw)); }
		msg strike()    { return from_md(   raw_strike(raw)); }
		static msg bold(     cstring in) { return from_md(     raw_bold(raw_escape(in))); }
		static msg italic(   cstring in) { return from_md(   raw_italic(raw_escape(in))); }
		static msg underline(cstring in) { return from_md(raw_underline(raw_escape(in))); }
		static msg strike(   cstring in) { return from_md(   raw_strike(raw_escape(in))); }
		
		static msg url(cstring in) { return from_md(in); }
		
		msg operator+(const msg& right) { return from_md(raw + right.raw); }
		msg operator+(cstring right) { return from_md(raw + right); }
		msg operator+(const char * right) { return from_md(raw + right); }
	};
	
	
	
	void connect_bot(cstring token);
	
	class Role;
	class User;
	class Channel;
	class Guild;
	friend class Role;
	friend class User;
	friend class Channel;
	friend class Guild;
	
	class Role {
		Discord* m_parent;
		string m_id;
		friend class Discord;
		//friend class Role;
		friend class User;
		friend class Channel;
		friend class Guild;
		
		i_role& impl() { return m_parent->roles[m_id]; }
		Role(Discord* parent, cstring id) : m_parent(parent), m_id(id) {}
		
	public:
		Role() : m_parent(NULL) {}
		Role(nullptr_t) : m_parent(NULL) {}
		Role(const Role& other) : m_parent(other.m_parent), m_id(other.m_id) {}
		
		string id() { return m_id; }
		string name() { return m_parent->roles[m_id].name; }
		Guild guild() { return Guild(m_parent, impl().guild); }
		set<User> users()
		{
			cstring guild_id = impl().guild;
			return m_parent->users
				.where([&](const map<string,i_user>::node& user)->bool { return user.value.roles.contains(m_id); })
				.select([&](const map<string,i_user>::node& user)->User { return User(m_parent, user.key, guild_id); })
				;
		}
		
		operator bool() { return m_parent; }
		bool operator==(const Role& other) { return m_parent==other.m_parent && m_id==other.m_id; }
		
		size_t hash() const { return m_id.hash(); }
	};
	class User {
		Discord* m_parent;
		string m_id;
		string m_guild_id; // for fetching nick
		
		friend class Discord;
		friend class Role;
		//friend class User;
		friend class Channel;
		friend class Guild;
		
		i_user& impl() { return m_parent->users[m_id]; }
		User(Discord* parent, cstring id, cstring guild_id) : m_parent(parent), m_id(id), m_guild_id(guild_id) {}
		
	public:
		User() : m_parent(NULL) {}
		User(nullptr_t) : m_parent(NULL) {}
		User(const User& other) : m_parent(other.m_parent), m_id(other.m_id) {}
		
		cstring id() { return m_id; }
		cstring nick()
		{
			string guild_nick = impl().nicks.get_or(m_guild_id, "");
			if (guild_nick) return guild_nick;
			else return impl().username;
		}
		string account() { return impl().username+"#"+impl().discriminator; }
		msg highlight() { return msg::from_md("<@"+m_id+">"); }
		
		bool is_bot() { return impl().is_bot; }
		
		array<Role> roles();
		array<Role> roles(Guild guild);
		bool has_role(Role role)
		{
			return impl().roles.contains(role.m_id);
		}
		void set_role(Role role, bool present)
		{
			m_parent->http(present ? "PUT" : "DELETE", "/guilds/"+role.impl().guild+"/members/"+m_id+"/roles/"+role.m_id);
		}
		
		void send(cstring text);
		
		bool partial() { return impl().username==""; }
		//Fetches account data about this user.
		void fetch(function<void(User)> callback);
		
		operator bool() { return m_parent; }
		bool operator==(const User& other) { return m_parent==other.m_parent && m_id==other.m_id; }
		
		size_t hash() const { return m_id.hash(); }
	};
	User user_from_id(cstring id) { return User(this, id, ""); }
	
	class Channel {
		Discord* m_parent;
		string m_id;
		friend class Discord;
		friend class Role;
		friend class User;
		//friend class Channel;
		friend class Guild;
		
		i_channel& impl() { return m_parent->channels[m_id]; }
		Channel(Discord* parent, cstring id) : m_parent(parent), m_id(id) {}
		
	public:
		Channel() : m_parent(NULL) {}
		Channel(nullptr_t) : m_parent(NULL) {}
		Channel(const Channel& other) : m_parent(other.m_parent), m_id(other.m_id) {}
		
		cstring id() { return m_id; }
		cstring name() { return impl().name; } // without #
		Guild guild() { return Guild(m_parent, impl().guild); }
		
		void rawmsg(cstring markdown)
		{
			JSON json;
			json["content"] = markdown;
			m_parent->http("/channels/"+m_id+"/messages", json);
		}
		
		void message(msg data) { rawmsg(data.raw); }
		void operator()(msg data) { message(data); }
		
		void busy()
		{
			m_parent->http("POST", "/channels/"+m_id+"/typing");
		}
		
		operator bool() { return m_parent; }
		bool operator==(const Channel& other) { return m_parent==other.m_parent && m_id==other.m_id; }
		
		size_t hash() const { return m_id.hash(); }
	};
	class Guild { // apparently Guild is to the API what Server is to the end user
		Discord* m_parent;
		string m_id;
		friend class Discord;
		friend class Role;
		friend class User;
		friend class Channel;
		//friend class Guild;
		
		i_guild& impl() { return m_parent->guilds[m_id]; }
		Guild(Discord* parent, cstring id) : m_parent(parent), m_id(id) {}
		
	public:
		Guild() : m_parent(NULL) {}
		Guild(nullptr_t) : m_parent(NULL) {}
		Guild(const Guild& other) : m_parent(other.m_parent), m_id(other.m_id) {}
		
		cstring id() { return m_id; }
		
		set<Role> roles() { return impl().roles.select([&](cstring r){ return Role(m_parent, r); }); }
		set<User> users() { return impl().users.select([&](cstring u){ return User(m_parent, u, m_id); }); }
		set<Channel> channels() { return impl().channels.select([&](cstring c){ return Channel(m_parent, c); }); }
		
		Role role(cstring name)
		{
			string id = impl().roles.first([&](cstring r)->bool { return m_parent->roles[r].name == name; });
			if (id) return Role(m_parent, id);
			else return Role();
		}
		Channel channel(cstring name)
		{
			string id = impl().channels.first([&](cstring c)->bool { return m_parent->channels[c].name == name; });
			if (id) return Channel(m_parent, id);
			else return Channel();
		}
		
		void change_nick(User user, string newnick)
		{
			if (!user || user.id()==m_parent->my_user)
			{
				JSON json;
				json["nick"] = newnick;
				m_parent->http("PATCH", "/guilds/"+id()+"/members/@me/nick", json);
			}
			else
			{
				JSON json;
				json["nick"] = newnick;
				m_parent->http("PATCH", "/guilds/"+id()+"/members/"+user.id(), json);
			}
		}
		
		operator bool() { return m_parent; }
		bool operator==(const Guild& other) { return m_parent==other.m_parent && m_id==other.m_id; }
		
		size_t hash() const { return m_id.hash(); }
	};
	
	User self() { return User(this, my_user, ""); }
	
	void self_rename(cstring newname)
	{
		JSON json;
		json["username"] = newname;
		http("PATCH", "/users/@me", json);
	}
	
	enum game_t {
		gt_play = 0,
		//gt_stream = 1, // buggy, treated as Playing unless url is present
		gt_listen = 2,
		gt_watch = 3,
	};
	void self_game(cstring game, game_t type)
	{
		JSON json;
		json["op"] = 3;
		json["d"]["game"]["name"] = game;
		json["d"]["game"]["type"] = (int)type;
		json["d"]["since"] = NULL; // some of these hardcoded entries are mandatory; if absent, gateway throws 'unknown opcode'
		json["d"]["status"] = "online"; // probably discord bug
		json["d"]["afk"] = false;
		send_ws(json);
	}
	
	void self_game(cstring game)
	{
		JSON json;
		json["op"] = 3;
		json["d"]["game"]["name"] = game;
		json["d"]["game"]["type"] = 0;
		json["d"]["since"] = NULL; // some of these hardcoded entries are mandatory; if absent, gateway throws 'unknown opcode'
		json["d"]["status"] = "online"; // probably discord bug
		json["d"]["afk"] = false;
		send_ws(json);
	}
	
	class callbacks {
	public:
		virtual void on_connect(Guild guild, User self) {}
		
		virtual void on_msg(Channel chan, User user, cstring message) {}
		
		virtual void on_guild_join(Guild guild, User user) {}
		virtual void on_guild_leave(Guild guild, User user) {}
	};
	callbacks* cb;
	
private:
	void process(bool block);
	
	runloop* loop;
	bool connecting = false;
	WebSocket m_ws; // be careful about using these directly, dangerous!
	HTTP m_http;
	
	void connect();
	void connect_cb(HTTP::rsp r);
	
	void ws_str(string text);
	
	bool bot;
	string token;
	
	time_t ratelimit = 0;
	
	int keepalive_ms = 30000;
	bool keepalive_sent;
	
	uintptr_t keepalive_id = 0;
	bool keepalive_cb();
	void update_keepalive_cb()
	{
		loop->remove(keepalive_id);
		keepalive_id = loop->set_timer_rel((keepalive_ms > 30000 ? keepalive_ms : 30000), bind_this(&Discord::keepalive_cb));
	}
	
	int guilds_to_join;
	
	//string resume;
	uint64_t sequence;
	
	struct i_role {
		string name;
		string guild;
	};
	map<string,i_role> roles;
	
	struct i_channel {
		string name;
		string guild;
		set<string> users;
	};
	map<string,i_channel> channels;
	
	struct i_user {
		string username; // Kieran
		string discriminator; // 4697 (it's split for some reason)
		map<string,string> nicks; // varies per guild
		set<string> roles; // also per-guild, but roles are unique across discord so excess roles don't do any harm
		bool is_bot = false;
	};
	map<string,i_user> users;
	string my_user;
	
	struct i_guild {
		string name;
		set<string> users;
		set<string> roles;
		set<string> channels;
	};
	map<string,i_guild> guilds;
	
	void headers(array<string>& h);
	
	void http(HTTP::req r, function<void(HTTP::rsp)> callback = NULL);
	void http(cstring url, function<void(HTTP::rsp)> callback = NULL)
	{
		HTTP::req r(url);
		http(r, callback);
	}
	void http(cstring url, JSON& post, function<void(HTTP::rsp)> callback = NULL)
	{
		HTTP::req r(url);
		r.postdata = post.serialize().bytes();
		http(r, callback);
	}
	void http(cstring method, cstring url, function<void(HTTP::rsp)> callback = NULL)
	{
		HTTP::req r(url);
		r.method = method;
		http(r, callback);
	}
	void http(cstring method, cstring url, JSON& post, function<void(HTTP::rsp)> callback = NULL)
	{
		HTTP::req r(url);
		r.method = method;
		r.postdata = post.serialize().bytes();
		http(r, callback);
	}
	
	void send_ws(JSON& json)
	{
		string msg = json.serialize();
		datelog("<< "+msg);
		m_ws.send(msg);
	}
	
	//takes a User object, with ["id"]
	void set_user_inner(JSON& json);
	//takes a Guild Member object, with ["user"] and ["roles"]
	void set_user(cstring guild_id, JSON& json);
	
	void del_user(cstring guild_id, cstring user_id);
	
	static string getdate();
	void datelog(cstring text);
	
	
public:
void debug();
string debug_connect;
time_t debug_next = 0;
Channel debug_target;
void debug_reset() { m_ws.reset(); }
};

inline Discord::msg operator+(cstring left, Discord::msg right) { return Discord::msg::from_md(left + right.raw); }
inline Discord::msg operator+(const char * left, Discord::msg right) { return Discord::msg::from_md((string)left + right.raw); }
