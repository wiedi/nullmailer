#include "config.h"
#include <ctype.h>
#include "canonicalize.h"
#include "mystring.h"
#include "list.h"

#define LSQBRACKET '['
#define RSQBRACKET ']'
#define QUOTE '"'
#define CR '\n'
#define LPAREN '('
#define RPAREN ')'

enum node_type {
  EMPTY = 0,
  // Full tokens, with string content:
  ATOM = 'A',
  QUOTED_STRING = 'Q',
  DOMAIN_LITERAL = 'D',
  COMMENT = 'C',
  // Special characters, no content:
  LABRACKET = '<',
  RABRACKET = '>',
  AT = '@',
  COMMA = ',',
  SEMICOLON = ';',
  COLON = ':',
  ESCAPE = '\\',
  PERIOD = '.',
  // End of tokens
  EOT = '$',
};

struct token
{
  const node_type type;
  const mystring str;

  token(node_type);
  token(node_type, mystring);
};

token::token(node_type t)
  : type(t)
{
}

token::token(node_type t, mystring s)
  : type(t), str(s)
{
}

struct anode : public token
{
  anode* next;
  anode(node_type);
  anode(node_type, const char*, const char*);
  anode(node_type, mystring);
};

anode::anode(node_type t)
  : token(t), next(0)
{
}

anode::anode(node_type t, const char* start, const char* end)
  : token(t, mystring(start, end-start)), next(0)
{
}

anode::anode(node_type t, mystring s)
  : token(t, s), next(0)
{
}

struct result
{
  anode* next;
  mystring str;
  mystring comment;
  mystring addr;

  result(const result&);
  result(anode* = 0);
  result(anode*, const mystring&, const mystring&, const mystring&);
  bool operator!() const
    {
      return !next;
    }
  operator bool() const
    {
      return next;
    }
};

result::result(anode* n)
  : next(n)
{
}

result::result(anode* n, const mystring& s,
	       const mystring& c, const mystring& l)
  : next(n), str(s), comment(c), addr(l)
{
}

result::result(const result& r)
  : next(r.next), str(r.str), comment(r.comment), addr(r.addr)
{
}

#ifndef TRACE
#define ENTER(R)
#define FAIL(MSG) return result()
#define RETURNR(R) return R
#define RETURN(N,S,C,L) return result(N,S,C,L)
#else
#include "fdbuf.h"
static const char indentstr[] = "                       ";
static const char* indent = indentstr + sizeof indentstr - 1;
#define ENTER(R) do{ fout << indent-- << __FUNCTION__ << ": " << node->str << ": " << R << endl; }while(0)
#define FAIL(MSG) do{ fout << ++indent << __FUNCTION__ << ": failed: " << MSG << endl; return result(); }while(0)
#define RETURNR(R) do{ fout << ++indent << __FUNCTION__ << ": succeded str=" << R.str << " comment=" << R.comment << " addr=" << R.addr << endl; return (R); }while(0)
#define RETURN(N,S,C,L) do{ result r(N,S,C,L); RETURNR(r); }while(0)
#endif

#define RULE(X) static result match_##X(anode* node)
#define MATCHTOKEN(X) do{ if(node->type != X) FAIL("node is not type " #X); else node = node->next; }while(0)
#define MATCHRULE(V,R) result V = match_##R(node); if(!V) FAIL("did not match " #R);
#define OR_RULE(ALT1,ALT2) { result r=match_##ALT1(node); if(r) RETURNR(r); }{ result r=match_##ALT2(node); if(r) RETURNR(r); } FAIL("did not match " #ALT1 " OR " #ALT2);

static bool issymbol(char c)
{
  switch(c) {
  case LPAREN: case RPAREN:
  case LABRACKET: case RABRACKET:
  case LSQBRACKET: case RSQBRACKET:
  case AT: case COMMA:
  case SEMICOLON: case COLON:
  case ESCAPE: case QUOTE: case PERIOD:
    return true;
  default:
    return false;
  }
}

static bool isctl(char c)
{
  return (c >= 0 && c <= 31) || (c == 127);
}
  
static bool isqtext(char c)
{
  return c && c != QUOTE && c != ESCAPE && c != CR;
}

static bool isdtext(char c)
{
  return c && c != LSQBRACKET && c != RSQBRACKET &&
    c != ESCAPE && c != CR;
}

// quoted-pair = ESCAPE CHAR
static bool isqpair(const char* ptr)
{
  return *ptr && *ptr == ESCAPE &&
    *(ptr+1);
}

static bool isatom(char ch)
{
  return !(isspace(ch) || issymbol(ch) || isctl(ch));
}

static anode* tokenize_atom(const char* &ptr)
{
  if(!isatom(*ptr)) return 0;
  const char* start = ptr;
  do {
    ++ptr;
  } while(isatom(*ptr));
  return new anode(ATOM, start, ptr);
}

static anode* tokenize_comment(const char* &ptr)
{
  if(*ptr != LPAREN) return 0;
  unsigned count = 0;
  const char* start = ptr;
  char ch = *ptr;
  while(ch) {
    if(isqpair(ptr))
      ++ptr;
    else if(ch == LPAREN)
      ++count;
    else if(ch == RPAREN) {
      --count;
      if(!count)
	return new anode(COMMENT, start, ++ptr);
    }
    else if(ch == CR)
      return 0;			// ERROR
    ++ptr;
    ch = *ptr;
  }
  return 0;			// ERROR
}

static anode* tokenize_domain_literal(const char* &ptr)
{
  if(*ptr != LSQBRACKET) return 0;
  const char* start = ptr;
  ++ptr;
  while(isspace(*ptr)) ++ptr;
  for(; *ptr; ++ptr) {
    if(isdtext(*ptr))
      continue;
    else if(isqpair(ptr))
      ++ptr;
    else
      break;
  }
  while(isspace(*ptr)) ++ptr;
  if(*ptr != RSQBRACKET)
    return 0;			// ERROR
  return new anode(DOMAIN_LITERAL, start, ptr);
}

static anode* tokenize_quoted_string(const char* &ptr)
{
  if(*ptr != QUOTE) return 0;
  const char* start = ptr;
  for(++ptr; *ptr; ++ptr) {
    if(isqtext(*ptr))
      continue;
    else if(isqpair(ptr))
      ++ptr;
    else
      break;
  }
  if(*ptr != QUOTE) return 0;
  ++ptr;
  return new anode(QUOTED_STRING, start, ptr);
}

static anode* tokenize(const char* &ptr)
{
  while(isspace(*ptr)) ++ptr;
  char ch = *ptr;
  switch(ch) {
  case 0:
    return new anode(EOT);
  case LABRACKET:
  case RABRACKET:
  case AT:
  case COMMA:
  case SEMICOLON:
  case COLON:
  case ESCAPE:
  case PERIOD:
    ++ptr;
    return new anode((node_type)ch);
  case LPAREN:
    return tokenize_comment(ptr);
  case LSQBRACKET:
    return tokenize_domain_literal(ptr);
  case QUOTE:
    return tokenize_quoted_string(ptr);
  default:
    return tokenize_atom(ptr);
  }
}

anode* tokenize(const mystring str)
{
  anode* head = new anode(EMPTY);
  anode* tail = head;
  anode* tmp;
  const char* ptr = str.c_str();
  while((tmp = tokenize(ptr)) != 0) {
    tail = tail->next = tmp;
    if(tmp->type == EOT) {
      tail = head->next;
      delete head;
      return tail;
    }
  }
  return 0;
}

static mystring quote(const mystring& in)
{
  unsigned length = in.length();
  // The result will never be more than double the length of the input plus 2
  char out[length*2 + 2 + 1];
  char* ptrout = out;
  const char* ptrin = in.c_str();
  bool quoted = false;
  for(; length; ++ptrin, ++ptrout, --length) {
    if(issymbol(*ptrin)) {
      *ptrout++ = ESCAPE;
      quoted = true;
    }
    *ptrout = *ptrin;
  }
  *ptrout = 0;
  if(quoted)
    return mystringjoin("\"") + out + "\"";
  else
    return in;
}

static mystring unquote(const mystring& in)
{
  unsigned length = in.length();
  // The result will never be more than the length of the input
  char out[length+1];
  bool modified = false;
  const char* ptrin = in.c_str();
  char* ptrout = out;
  if(in[0] == QUOTE && in[length-1] == QUOTE) {
    length -= 2;
    ptrin++;
    modified = true;
  }
  for(; length; ++ptrin, ++ptrout, --length) {
    if(isqpair(ptrin)) {
      ++ptrin;
      --length;
      modified = true;
    }
    *ptrout = *ptrin;
  }
  *ptrout = 0;
  if(modified)
    return out;
  else
    return in;
}

anode* skipcomment(anode* node, mystring& comment)
{
  while(node->type == COMMENT) {
    comment = comment + " " + node->str;
    node = node->next;
  }
  return node;
}
    
RULE(sub_domain)
{
  // Note atom <= domain-ref
  ENTER("atom / domain-literal");
  mystring comment;
  node = skipcomment(node, comment);
  if(node->type == ATOM || node->type == DOMAIN_LITERAL)
    RETURN(node->next, node->str, comment, node->str);
  FAIL("did not match ATOM or DOMAIN-LITERAL");
}

RULE(domain)
{
  ENTER("sub-domain *(PERIOD sub-domain)");
  MATCHRULE(r, sub_domain);
  if(!r) FAIL("did not match sub-domain");
  mystring comment;
  for(;;) {
    node = r.next = skipcomment(r.next, comment);
    if(node->type != PERIOD)
      break;
    node = node->next;
    result r1 = match_sub_domain(node);
    if(!r1) break;
    r.next = r1.next;
    r.str += PERIOD;
    r.str += r1.str;
    comment += r1.comment;
    r.addr += PERIOD;
    r.addr += r1.addr;
  }
  r.comment += comment;
  RETURNR(r);
}

RULE(route)
{
  ENTER("1#(AT domain) COLON");
  unsigned count=0;
  mystring str;
  mystring comment;
  for(;;) {
    if(node->type != AT) break;
    node = node->next;
    MATCHRULE(r, domain);
    str += "@";
    str += r.str;
    comment += r.comment;
    ++count;
    node = r.next;
  }
  if(count == 0)
    FAIL("matched no domains");
  node = skipcomment(node, comment);
  MATCHTOKEN(COLON);
  RETURN(node, str, comment, "");
}

RULE(word)
{
  ENTER("atom / quoted-string");
  mystring comment;
  node = skipcomment(node, comment);
  if(node->type == ATOM)
    RETURN(node->next, node->str, comment, node->str);
  else if(node->type == QUOTED_STRING) {
    mystring addr = unquote(node->str);
    RETURN(node->next, quote(addr), comment, addr);
  }
  FAIL("did not match ATOM or QUOTED-STRING");
}

RULE(local_part)
{
  ENTER("word *(PERIOD word)");
  MATCHRULE(r, word);
  for(;;) {
    node = r.next = skipcomment(r.next, r.comment);
    if(node->type != PERIOD)
      break;
    node = node->next;
    result r1 = match_word(node);
    if(!r1)
      break;
    r.next = r1.next;
    r.str += ".";
    r.str += r1.str;
    r.comment += r1.comment;
    r.addr += ".";
    r.addr += r1.addr;
  }
  RETURNR(r);
}
  
RULE(addr_spec)
{
  ENTER("local-part *( AT domain )");
  MATCHRULE(r, local_part);
  mystring domain;
  for(;;) {
    node = r.next = skipcomment(r.next, r.comment);
    if(node->type != AT)
      break;
    node = node->next;
    result r2 = match_domain(node);
    if(!r2) break;
    if(!!domain) {
      r.str += "@";
      r.str += domain;
      r.addr += "@";
      r.addr += domain;
    }
    domain = r2.addr;
    r.comment += r2.comment;
    r.next = r2.next;
  }
  canonicalize(domain);
  RETURN(r.next, r.str + "@" + domain, r.comment,
	 r.addr + "@" + domain + "\n");
}

RULE(route_addr) 
{
  ENTER("LABRACKET [route] addr-spec RABRACKET");
  mystring comment;
  node = skipcomment(node, comment);
  MATCHTOKEN(LABRACKET);
  result r1 = match_route(node);
  if(r1) node = r1.next;
  comment += r1.comment;
  MATCHRULE(r2, addr_spec);
  node = r2.next;
  comment += r2.comment;
  node = skipcomment(node, comment);
  MATCHTOKEN(RABRACKET);
  RETURN(node, "<" + r2.str + ">" + comment, "", r2.addr);
}

RULE(phrase)
{
  ENTER("word *word");
  MATCHRULE(r1, word);
  for(;;) {
    result r2 = match_word(r1.next);
    if(!r2)
      break;
    r1.str += " ";
    r1.str += r2.str;
    r1.comment += r2.comment;
    r1.next = r2.next;
  }
  RETURNR(r1);
}

RULE(route_spec)
{
  ENTER("[phrase] route-addr");
  result r1 = match_phrase(node);
  if(r1)
    node = r1.next;
  MATCHRULE(r2, route_addr);
  if(!r1)
    RETURNR(r2);
  r2.str = r1.str + r1.comment + " " + r2.str + r2.comment;
  RETURNR(r2);
}

RULE(mailbox)
{
  ENTER("route-spec / addr-spec");
  OR_RULE(route_spec, addr_spec);
}

RULE(mailboxes)
{
  ENTER("mailbox *(*(COMMA) mailbox)");
  MATCHRULE(r1, mailbox);
  r1.str += r1.comment;
  r1.comment = "";
  for(;;) {
    node = r1.next;
    for(;;) {
      node = skipcomment(node, r1.str);
      if(node->type == COMMA) node = node->next;
      else break;
    }
    if(node->type == EOT)
      break;
    result r2 = match_mailbox(node);
    if(!r2) break;
    r1.next = r2.next;
    r1.str = r1.str + ", " + r2.str + r2.comment;
    r1.addr += r2.addr;
  }
  node = skipcomment(node, r1.str);
  r1.next = node;
  RETURNR(r1);
}

RULE(group)
{
  ENTER("phrase COLON [#mailboxes] SEMICOLON");
  MATCHRULE(r1, phrase);
  node = r1.next;
  MATCHTOKEN(COLON);
  result r2 = match_mailboxes(node);
  if(r2) node = r2.next;
  mystring comment;
  node = skipcomment(node, comment);
  MATCHTOKEN(SEMICOLON);
  RETURN(node, r1.str + ": " + r2.str + r2.comment + comment + ";",
	 "", r2.addr);
}

RULE(address)
{
  ENTER("group / mailbox");
  OR_RULE(group, mailbox);
}

RULE(addresses)
{
  ENTER("address *(*(COMMA) address) EOF");
  MATCHRULE(r1, address);
  r1.str += r1.comment;
  r1.comment = "";
  for(;;) {
    node = r1.next;
    for(;;) {
      node = skipcomment(node, r1.str);
      if(node->type == COMMA) node = node->next;
      else break;
    }
    if(node->type == EOT)
      break;
    result r2 = match_address(node);
    if(!r2) break;
    r1.next = r2.next;
    r1.str = r1.str + ", " + r2.str + r2.comment;
    r1.addr += r2.addr;
  }
  node = skipcomment(node, r1.str);
  if(node->next) FAIL("Rule ended before EOF");
  RETURNR(r1);
}

static void del_tokens(anode* node)
{
  while(node) {
    anode* tmp = node->next;
    delete node;
    node = tmp;
  }
}

bool parse_addresses(mystring& line, mystring& list)
{
  anode* tokenlist = tokenize(line.c_str());
  if(!tokenlist)
    return false;
  result r = match_addresses(tokenlist);
  del_tokens(tokenlist);
  if(r) {
    line = r.str;
    list = r.addr;
    return true;
  }
  else
    return false;
}