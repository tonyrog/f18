%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%     F18 assembler/disassembler 
%%% @end
%%% Created : 14 Feb 2026 by Tony Rogvall <tony@rogvall.se>

-module(f18_asm).

-export([file/1]).
-export([send/1, send/2]).
-export([read_lines/1, read_lines/2]).
-export([tokenize_lines/1]).
-export([asm_lines/1, asm_lines/2]).
-export([opcode/2, opcode/1]).
-export([parse_op/2, parse_op/1]).

-export([baud/1]).
-export([demo/0]).

-include("f18.hrl").

-define(dbg(F,A), io:format((F),(A))).

opcode(Op, common) ->
    case Op of
	';'     -> 16#00;
	'ex'    -> 16#01;
	'jump'  -> 16#02;  %% syntax <name> ';' (tail call?)
	'call'  -> 16#03;
	'unext' -> 16#04;
	'next'  -> 16#05;
	'if'    -> 16#06;
	'-if'   -> 16#07;
	%% Memory Read and Write
	'@p'    -> 16#08;
	'@+'    -> 16#09;
	'@b'    -> 16#0A;
	'@'     -> 16#0B;
	'!p'    -> 16#0C;
	'!+'    -> 16#0D;
	'!b'    -> 16#0E;
	'!'     -> 16#0F;
	%% Arithmetic; Logic and Register manipulation
	'+*'    -> 16#10;
	'2*'    -> 16#11;
	'2/'    -> 16#12;
	'+'     -> 16#14;
	'and'   -> 16#15;
	'drop'  -> 16#17;
	'dup'   -> 16#18;
	'over'  -> 16#1A;
	'a'     -> 16#1B;
	'.'     -> 16#1C;
	'b!'    -> 16#1E;
	'a!'    -> 16#1F
    end;
opcode(Op, v110412) ->
    case Op of
	'-'     -> 16#13;
	'or'    -> 16#16;
	'pop'   -> 16#19;
	'push'  -> 16#1D;
	_ -> opcode(Op, common)
    end;
opcode(Op, v221113) ->
    case Op of
	'inv'     -> 16#13;
	'xor'    -> 16#16;
	'r>'   -> 16#19;
	'>r'  -> 16#1D;
	_ -> opcode(Op, common)
    end;
opcode(Op, compatible) ->
    case Op of
	'-'     -> 16#13;
	'or'    -> 16#16;
	'pop'   -> 16#19;
	'push'  -> 16#1D;
	'inv'   -> 16#13;
	'xor'   -> 16#16;
	'r>'    -> 16#19;
	'>r'    -> 16#1D;
	_ -> opcode(Op, common)
    end.
opcode(Op) ->
    opcode(Op, ?DEFAULT_VSN).

parse_op(Op, common) ->
    case Op of
	";" -> ';';
	"ex" -> 'ex';
	"unext" -> 'unext';
	"@p" -> '@p';
	"@+" -> '@+';
	"@b" -> '@b';
	"@"  -> '@';
	"!p" -> '!p';
	"!+" -> '!+';
	"!b" -> '!b';
	"!"  -> '!';
	"+*" -> '+*';
	"2*" -> '2*';
	"2/" -> '2/';
	"+"   -> '+';
	"and" -> 'and';
	"drop" -> 'drop';
	"dup"  -> 'dup';
	"over" -> 'over';
	"a" -> 'a';
	"." -> '.';
	"b!" -> 'b!';
	"a!" -> 'a!'
    end;
parse_op(Op, v110412) ->
    case Op of
	"-" -> '-';
	"or" -> 'or';
	"pop" -> 'pop';
	"push" -> 'push';
	_ -> opcode(Op, common)
    end;
parse_op(Op, v221113) ->
    case Op of
	"inv" -> 'inv';
	"xor" -> 'xor';
	"r>"  -> 'r>';
	">r"  -> '>r';
	_ -> opcode(Op, common)
    end;
parse_op(Op, compatible) ->
    case Op of
	"inv" -> 'inv';
	"-"   -> 'inv';
	"xor" -> 'xor';
	"or"  -> 'xor';
	"r>"  -> 'r>';
	">r"  -> '>r';
	"pop" -> 'r>';
	"push" -> '>r';
	_ -> opcode(Op, common)
    end.
parse_op(Op) ->
    opcode(Op, ?DEFAULT_VSN).

iodest() ->
    #{  "io" => 16#15D,
	"data" => 16#141,
	"---u" => 16#145,
	"ldata" => 16#171,
	"--l-" => 16#175,
	"--lu" => 16#165,
	"-d--" => 16#115,
	"-d-u" => 16#105,
	"-dl-" => 16#135,
	"-dlu" => 16#125,
	"r---" => 16#1D5,
	"r--u" => 16#1C5,
	"r-l-" => 16#1F5,
	"r-lu" => 16#1E5,
	"rd--" => 16#195,
	"rd-u" => 16#185,
	"rdl-" => 16#1B5,
	"rdlu" => 16#1A5,
	%% special hack fixme 
	"stdin"  => 16#100,
	"stdout" => 16#101,
	"stdio"  => 16#102,
	"tty"    => 16#103
     }.

macros() ->
    #{  
	'band' => ['and'],
	'bnot' => [inv],
	'bxor' => ['xor'],
	'swap' => [over,'>r','>r',drop,'r>','r>'],
	%% A or B == (A xor B) xor (A and B)
	%% A or B == ~(~A and ~B)
	%% 
	%% bor = [ 'swap', 'bnot', 'band', 'bnot'],
	'bor' => ['bnot', 'over', 'bnot', 'band', 'bnot', 
		  '>r', drop, 'r>'],
	%% 
	'zero' => [dup,dup,'xor'],
	%% A--
	'dec' => ['zero','bnot','+']
     }.

baud(Opts) ->
    B = maps:get(baud, Opts, ?DEFAULT_BAUD),
    ?UDELAY(B).

%% RAM code needed to send words and bytes
code708(Opts) ->
    RAM = 
	[
	 [{word,"node"}, 708],
	 %% : send = 0
	 ['dup','dup','xor','.'],   %% push 0 
	 [{call,{label,"_send8"}}],
	 [drop,{call,{label,"_send8"}}],
	 [{call,{label,"_send8"}}],
	 %% : _send8 = 4
	 ['dup','dup','xor','.'],  %% push 0 
	 [{call,{label,"_send1"}}],
	 ['@p', '>r', '.', '.'],
	 [7],
	 %% : _loop = 8
	 [dup,{call,{label,"_send1"}}],
	 ['2/',{next,{label,"_loop"}}],
	 ['@p','.','.','.'],
	 [1],
	 %% : _send1 = 12
	 ['@p', 'and', '@p', '.'],
	 [1],
	 [3],
	 ['xor','!b','@p','.'],
	 [baud(Opts)],
	 ['>r', '.', '.', '.'],
	 ['unext', ';', '.', '.'],
	 %% : exit = 19
	 ['@p', {jump,{label,"_send8"}}],
	 [1]
	],
    Opts1 = Opts#{ {def,"send"} => 0,
		   {def,"_send8"} => 4,
		   {def,"_loop"} => 8,
		   {def,"_send1"} => 12,
		   {def,"exit"} => 19
	   },
    UPLOAD = 
	[
	 ['@p', 'a!', '@p', '-'],
	 [0],
	 [length(RAM)-1],
	 ['>r', '.', '.', '.'],
	 ['@p', '!+', 'unext', '.']
	],
    %% TEST function
    STREAM = 
	UPLOAD ++ RAM ++
	[
	 %% ['@p', 'b!','.','.'],
	 %% [{word,"io"}],
	 ['@p', {call,{label,"send"}}],
	 [$O],
	 [drop, '@p', '.', '.'],
	 [$K],
	 [{call,{label,"send"}}],
	 [drop, '@p', '.', '.'],
	 [$\n],
	 [{call,{label,"send"}}],
	 [drop, {call, {label,"exit"}}]
	],
    {STREAM, Opts1}.

demo() ->
    Opts = #{ rom => basic, encode => true, baud => 115200 },
    {Lines,Opts1} = code708(Opts),
    {ok, Code} = asm_lines(Lines, Opts1),
    {ok,U,Pid} = start_uart(Opts1),
    f18_uart:send(U, Code),
    Pid ! stop.

send(Filename) ->
    send(Filename, #{ rom => basic, encode => true }).
send(Filename, Opts) ->
    case file(Filename, Opts) of
	{ok, Lines} ->
	    {ok,U,Pid} = start_uart(Opts),
	    f18_uart:send(U, Lines),
	    Pid ! stop;
	Error ->
	    Error
    end.

start_uart(Opts) ->
    Caller = self(),
    Pid = spawn_link(
	    fun() ->
		    Baud = maps:get(baud, Opts, 460800),
		    {ok, U} = f18_uart:open(target, async, [{baud,Baud}]),
		    uart:flush(U, both),
		    %% uart:controlling_process(U, Caller),
		    f18_uart:reset(U),
		    uart:setopts(U, [{active,true}]),
		    Caller ! {self(), U},
		    loop(U, infinity)
	    end),
    receive
	{Pid, U} ->
	    {ok,U,Pid}
    end.

loop(U, Timeout) ->
    receive
	{uart, U, Data} ->
	    io:format("UART: ~w\n", [binary_to_list(Data)]),
	    loop(U, Timeout);
	{uart_error,U,enxio} ->
	    {error, enxio};
	{uart_closed, U} ->
	    {error, closed};
	stop ->
	    loop(U, 1000);
	Got ->
	    io:format("UART Got: ~p\n", [Got]),
	    loop(U, Timeout)
    after Timeout ->
	    uart:close(U)
    end.
	      

file(Filename) ->
    file(Filename, #{ rom => basic }).
file(Filename, Opts) ->
    case read_lines(Filename, Opts) of
	{ok, Lines} ->
	    asm_lines(Lines, Opts);
	Error ->
	    Error
    end.

read_lines(Filename) ->
    read_lines(Filename, #{}).
read_lines(Filename, _Opts) ->
    case file:read_file(Filename) of
	{ok,Bin} ->
	    Lines = binary:split(Bin, <<"\n">>, [global]),
	    tokenize_lines(Lines, []);
	Error ->
	    Error
    end.

asm_lines(Lines) ->
    asm_lines(Lines,0,[],#{ node=>708 }).
asm_lines(Lines, Opts) ->
    asm_lines(Lines,0,[],Opts#{ node=>708 }).

asm_lines([[{word,":"},{word,W}|Line]|Lines],Addr,Acc,Opts) ->
    ?dbg("DEF ~s => ~w\n", [W,Addr]),
    asm_lines([Line|Lines],Addr,Acc,Opts#{ {def,W} => Addr });
asm_lines([[{word,"org"},Addr1|_]|Lines],_Addr,Acc,Opts) 
  when is_integer(Addr1) ->
    ?dbg("ORG ~w\n", [Addr1]),
    asm_lines(Lines,Addr1,Acc,Opts);
asm_lines([[{word,"node"},Node|_]|Lines],Addr,Acc,Opts) 
  when is_integer(Node) ->
    ?dbg("NODE ~w\n", [Node]),
    RomType = f18_rom:get_node_rom_type(Node),
    ?dbg("ROM = ~s\n", [RomType]),
    asm_lines(Lines,Addr,Acc,Opts#{ rom => RomType,
				    node => Node });
asm_lines([[]|Lines],Addr,Acc,Opts) ->
    asm_lines(Lines,Addr,Acc,Opts);
asm_lines([Line|Lines],Addr,Acc,Opts) ->
    Cell = asm_line(Line, Addr, Opts),
    asm_lines(Lines,Addr+1,[Cell|Acc],Opts);
asm_lines([],_Addr,Acc,_Opts) ->
    %% patch forward labels?
    {ok, lists:reverse(Acc)}.

asm_line([A],_Addr,_Opts) when is_integer(A) ->
    ?dbg("[0] literal ~w\n", [A]),
    A;
asm_line([{word,A}],Addr,Opts) ->
    case maps:find(A, iodest()) of
	{ok, Addr1} -> 
	    ?dbg("[0] literal ~w\n", [Addr1]),
	    Addr1;
	error -> 
	    asm_line([{call,A}],Addr,Opts)
    end;
asm_line([{call,A},';'],Addr,Opts) ->
    asm_line([{jump,A}], Addr, Opts);
asm_line([A,{call,B},';'],Addr,Opts) ->
    asm_line([A,{jump,B}], Addr, Opts);
asm_line([A,B,{call,C},';'],Addr,Opts) ->
    asm_line([A,B,{jump,C}], Addr, Opts);
asm_line([A,B,C,{call,D},';'],Addr,Opts) ->
    asm_line([A,B,C,{jump,D}], Addr, Opts); %% ?? check this
asm_line([A,B,C,D], Addr, Opts) ->
    asm_ops([A,B,C,D],0,13,0,Addr,Opts);
asm_line([A,B,C], Addr, Opts) ->
    asm_ops([A,B,C],0,13,0,Addr,Opts);
asm_line([A,B], Addr, Opts) ->
    asm_ops([A,B],0,13,0,Addr,Opts);
asm_line([A], Addr, Opts) ->
    asm_ops([A],0,13,0,Addr,Opts).

asm_ops([{JOp,{label,W}}],Slot,_Shift,Instr,Addr,Opts) when Slot < 3 ->
    Dest = case maps:find({def,W}, Opts) of
	       {ok, D} -> D;
	       error ->
		   Rom = maps:get(rom, Opts, basic),
		   RomSymbols = f18_rom:rom_symbols(),
		   SymTab = maps:get(Rom, RomSymbols),
		   maps:get(W, SymTab)
	   end,
    ?dbg("[~w] ~s:~s=~w ", [Slot,JOp,W,Dest]),
    Code = opcode(JOp, compatible),
    case Slot of
	2 ->
	    encode(Opts, Instr bor (Code bsl 3),
		   16#7, Addr, Dest);
	1 ->
	    encode(Opts, Instr bor (Code bsl 8),
		   16#ff, Addr, Dest);
	0 ->
	    encode(Opts, Instr bor (Code bsl 13),
		   16#1fff, Addr, Dest)
    end;
asm_ops([{JOp,Dest}],Slot,_Shift,Instr,Addr,Opts) when 
      is_integer(Dest), Slot < 3 ->
    ?dbg("[~w] ~s:~w ", [Slot,JOp,Dest]),
    Code = opcode(JOp, compatible),
    case Slot of
	2 -> 
	    encode(Opts, Instr bor (Code bsl 3),
		   16#7, Addr, Dest);
	1 -> 
	    encode(Opts, Instr bor (Code bsl 8),
		   16#ff, Addr, Dest);
	0 ->
	    encode(Opts, Instr bor (Code bsl 13),
		   16#1fff, Addr, Dest)
    end;
asm_ops([Op|Ops],Slot,Shift,Instr,Addr,Opts) ->
    ?dbg("[~w] ~s ", [Slot,Op]),
    Code = opcode(Op, compatible),
    asm_ops(Ops,Slot+1,Shift-5,
	    Instr bor (Code bsl Shift),Addr,Opts);
asm_ops([],Slot,Shift,Instr,Addr,Opts) when Slot =< 3 ->
    case Slot of
	1 -> asm_ops(['.','.','.'],Slot,Shift,Instr,Addr,Opts);
	2 -> asm_ops(['.','.'],Slot,Shift,Instr,Addr,Opts);
	3 -> asm_ops(['.'],Slot,Shift,Instr,Addr,Opts)
    end;
asm_ops([],Slot,_Shift,Instr, Addr,Opts) when Slot > 3 ->
    encode(Opts, Instr, 16#00000, Addr, 0).

%% fixme: have a encode := invert, that keep op but xor the dest!?
encode(#{ encode := true }, Instr, Mask, Addr, Dest) ->
    Code = ((Instr bxor 16#15555) band (bnot Mask)) bor 
	(Addr band (bnot Mask)) bor (Dest band Mask),
    ?dbg(" => |~s|\n", [integer_to_list(Code, 16)]),
    Code;
encode(_, Instr, Mask, Addr, Dest) ->
    Code = Instr bor (Addr band (bnot Mask)) bor (Dest band Mask),
    ?dbg(" => ~s\n", [integer_to_list(Code, 16)]),
    Code.
    
tokenize_lines([]) -> [];
tokenize_lines(Lines=[L|_]) when is_binary(L) ->
    tokenize_lines(Lines, []).

tokenize_lines([Line|Lines], Acc) ->
    case tokenize(Line) of
	[] ->
	    tokenize_lines(Lines, Acc);
	Ts ->
	    tokenize_lines(Lines, [Ts|Acc])
    end;
tokenize_lines([], Acc) ->
    {ok, lists:reverse(Acc)}.

tokenize(<<$\s, Rest/binary>>) -> tokenize(Rest);
tokenize(<<$\t, Rest/binary>>) -> tokenize(Rest);
tokenize(<<"\\", _/binary>>) -> [];
tokenize(<<>>) -> [];
tokenize(<<$(, Rest/binary>>) ->
    tokenize(skip(Rest, $(, $), 1));
tokenize(Bin) ->
    {W,Rest} = word(Bin,[$\s,$\t]), 
    W1 = parse_word(W),
    [W1|tokenize(Rest)].

word(Bin,Ds) ->
    word(Bin,Ds,[]).

word(<<C,Cs/binary>>,Ds,Acc) ->
    case lists:member(C, Ds) of
	true -> {lists:reverse(Acc), Cs};
	false -> word(Cs, Ds, [C|Acc])
    end;
word(<<>>, _Ds, Acc) ->
    {lists:reverse(Acc), <<>>}.

skip(<<CR,Bin/binary>>, CL, CR, I) ->
    if I =:= 1 -> Bin;
       true -> skip(Bin, CL, CR, I-1)
    end;
skip(<<CL,Bin/binary>>, CL, CR, I) -> skip(Bin, CL, CR, I+1);
skip(<<_,Bin/binary>>, CL, CR, N) -> skip(Bin, CL, CR, N);
skip(<<>>, _CL, _CR, _N) -> <<>>.

parse_word(W) ->
    case parse_inst(W) of
	{word,_} ->
	    try parse_int(W) of
		Int -> Int
	    catch
		error:_ ->
		    {word,W}
	    end;
	Inst -> Inst
    end.

parse_inst("jump:"++Addr) -> {jump,parse_dest(Addr)};
parse_inst("call:"++Addr) -> {call,parse_dest(Addr)};
parse_inst("if:"++Addr)   -> {'if',parse_dest(Addr)};
parse_inst("-if:"++Addr)  -> {'-if',parse_dest(Addr)};
parse_inst("next:"++Addr) -> {next, parse_dest(Addr)};

parse_inst(W) ->
    try parse_op(W, compatible) of
	Op -> Op
    catch
	error:_ ->
	    {word,W}
    end.

parse_dest(Addr) ->
    try parse_int(Addr) of
	A -> A
    catch
	error:_ ->
	    case maps:find(Addr, iodest()) of
		{ok, Addr1} -> Addr1;
		error -> {label,Addr}
	    end
    end.

parse_int([$0]) -> 0;
parse_int([$0,$x|Ds]) -> list_to_integer(Ds,16);
parse_int([$0,$b|Ds]) -> list_to_integer(Ds,2);
parse_int([$0|Ds]) -> list_to_integer(Ds,16);
parse_int(Ds) -> list_to_integer(Ds,10).
