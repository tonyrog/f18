%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%     Create a string table
%%% @end
%%% Created :  3 Mar 2026 by Tony Rogvall <tony@rogvall.se>

-module(f18_strings).

-export([generate/0]).
-export([encode_string_name/1]).
-export([encode_chars/1]).

generate() ->
    {ok, Bin} = file:read_file("f18_strings.tab"),
    Strings = binary:split(Bin, <<"\n">>, [global]),
    {ok, FdC} = file:open("f18_strings.c", [write]),
    lists:foreach(
      fun(String) ->
	      Name = encode_string_name(String),
	      io:format(FdC, "// string: \"~s\"\n", [String]),
	      io:format(FdC, "const char ~s[] = { 'R', ~w, ",
			[Name, byte_size(String)]),
	      Chars = [[ [integer_to_list(C), $,] || <<C>> <= String ],
		       "0"],
	      io:put_chars(FdC, [Chars, "};\n"])
      end, Strings),
    file:close(FdC),
    {ok, FdH} = file:open("f18_strings.h", [write]),
    io:format(FdH, "// generate by f18_strings.erl\n", []),
    io:format(FdH, "#ifndef __F18_STRINGS_H__\n", []),
    io:format(FdH, "#define __F18_STRINGS_H__\n\n", []),
    lists:foreach(
      fun(String) ->
	      Name = encode_string_name(String),
	      io:format(FdH, "extern const char ~s[];\n", [Name])
      end, Strings),
    io:format(FdH, "#endif\n", []),
    file:close(FdH),
    ok.
    
encode_string_name(String) ->
    lists:flatten([ "SN_" | encode_chars(String) ]).

encode_chars(String) when is_binary(String) ->
    encode_chars(binary_to_list(String), []);
encode_chars(String) when is_list(String) ->
    encode_chars(String, []).


encode_chars([], Acc) ->
    lists:join("_", lists:reverse(Acc));
encode_chars(String, Acc) ->
    {EChar, String1} = encode_char(String),
    encode_chars(String1, [EChar|Acc]).

encode_char([C|String])
  when C >= $a, C =< $z;
       C >= $A, C =< $Z;
       C >= $0, C =< $9;
       C =:= $_ -> 
    collect_name(String, [C]);
encode_char([C|String]) ->
    {maps:get(C, charmap()), String}.

collect_name([C|String], Acc)
  when C >= $a, C =< $z;
       C >= $A, C =< $Z;
       C >= $0, C =< $9;
       C =:= $_ -> 
    collect_name(String, [C|Acc]);
collect_name(String, Acc) ->
    {lists:reverse(Acc), String}.

charmap() ->
    #{ 
       $! => "BANG",
       $" => "QUOT",
       $# => "HASH",
       $$ => "DOLLAR",
       $% => "PCT",
       $& => "AMP",
       $\' => "APOS",
       $( => "LPAR",
       $) => "RPAR",
       $* => "STAR",
       $+ => "PLUS",
       $, => "COMMA",
       $- => "DASH",
       $. => "DOT",
       $/ => "SLASH",
       $: => "COLON",
       $; => "SEMI",
       $< => "LT",
       $= => "EQ",
       $> => "GT",
       $? => "QUEST",
       $@ => "AT",
       $[ => "LBRACK",
       $\\ => "BSLASH",
       $] => "RBRACK",
       $^ => "CARET",
       $` => "BACKT",
       ${ => "LBRACE",
       $| => "PIPE",
       $} => "RBRACE",
       $~ => "TILDE",
       $\s  => "SP"
     }.

    
