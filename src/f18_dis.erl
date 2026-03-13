%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%     Disassembler 
%%% @end
%%% Created : 17 Feb 2026 by Tony Rogvall <tony@rogvall.se>

-module(f18_dis).

%% -export([file/1, file/2]).
-export([words/1, words/2]).
-export([word/2, word/3]).
-export([decode_op/1, opcodes/0]).

-include("f18.hrl").

opcodes() ->
    opcodes(?DEFAULT_VSN).
opcodes(v221113) ->
    { 
      ';',  'ex', {'jump',dest}, {'call',dest},
      'unext', {'next',dest}, {'if',dest}, {'-if', dest},
      %% Memory Read and Write
      '@p','@+','@b','@',
      '!p','!+','!b','!',
       %% Arithmetic, Logic and Register manipulation
      '+*','2*','2/','inv',
      '+', 'and','xor','drop',
      'dup','r>','over', 'a',
       '.', '>r', 'b!', 'a!'
    };
opcodes(v110412) ->
    { 
      ';',  'ex', {'jump',dest}, {'call',dest},
      'unext', {'next',dest}, {'if',dest}, {'-if',dest},
      %% Memory Read and Write
      '@p','@+','@b','@',
      '!p','!+','!b','!',
       %% Arithmetic, Logic and Register manipulation
      '+*','2*','2/','-',
      '+', 'and','or','drop',
       'dup','pop','over', 'a',
       '.', 'push', 'b!', 'a!'
     }.

decode_op(Op) ->
    decode_op(Op, ?DEFAULT_VSN).
decode_op(Op, Vsn) ->
    element(Op+1, opcodes(Vsn)).

word(Addr,W) ->
    word(Addr, W, #{ version => ?DEFAULT_VSN }).
word(Addr,W,#{ version := Vsn}) ->
    %% decode slot 0
    case decode_op((W bsr 13) band 16#1f,Vsn) of 
	{JOp,dest} -> [{JOp, make_addr(Addr, W, 16#1fff)}];
	Op0 -> 
	    case decode_op((W bsr 8) band 16#1f,Vsn) of
		{JOp,dest} -> [Op0,{JOp, make_addr(Addr, W, 16#ff)}];
		Op1 ->
		    case decode_op((W bsr 3) band 16#1f,Vsn) of
			{JOp,dest} -> [Op0,Op1,{JOp,make_addr(Addr, W, 16#7)}];
			Op2 ->
			    Op3 = decode_op((W band 16#7) bsl 2,Vsn),
			    [Op0,Op1,Op2,Op3]
		    end
	    end
    end.

make_addr(Addr, W, SlotMask) ->
    (Addr band (bnot SlotMask)) bor (W band SlotMask).

words(Ws) ->
    words(Ws, #{ version => ?DEFAULT_VSN}).

words(Ws, Opts) ->
    Addr0 = maps:get(addr, Opts, 16#80),
    AddrList = [Addr0+I || I <- lists:seq(0, length(Ws)-1)],
    [word(Addr,W,Opts) || {Addr,W} <- lists:zip(AddrList, Ws)].
