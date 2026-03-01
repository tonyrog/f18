%%% @author Tony Rogvall <tony@rogvall.se>
%%% @copyright (C) 2026, Tony Rogvall
%%% @doc
%%%    Connect to F18 target (and host) chip via USB ports
%%% @end
%%% Created : 11 Feb 2026 by Tony Rogvall <tony@rogvall.se>

-module(f18_uart).

-export([open/2, open/3, close/1]).
-export([reset/1]).
-export([send/2, send_word/2]).
-export([encode/1]).

-include("f18.hrl").

open(Chip, Type) ->
    open(Chip, Type, []).

%% target chip is accessed via USB port C connected to node 708
open(target, async, Opts) ->
    uart:open1(?USB_C, [{baud,?DEFAULT_BAUD},{mode,binary}|Opts]);
%% host chip serdes port is connected to Node 701
open(host, async, Opts) ->  
    uart:open1(?USB_A, [{baud,?DEFAULT_BAUD},{mode,binary}|Opts]).

close(U) ->
    uart:close(U).

reset(U) ->
    uart:clear_modem(U, [rts]),
    timer:sleep(10),
    uart:set_modem(U,   [rts]),
    timer:sleep(10).

%% send list of 18-bit instructions (encoded)
send(U, Data) ->
    uart:send(U, encode_words_(Data,[])).

%% encode data to be transmited to async node (like 708)
encode(Data) ->
    encode_words_(Data, []).

encode_words_([W|Data], Acc) ->
    encode_words_(Data, [encode_word(W) | Acc]);
encode_words_([], Acc) ->
    lists:reverse(Acc).

send_word(U, W) ->
    ok = uart:send(U, encode_word(W)).

%% 18bit word:     xxxxxxxxyyyyyyyyzz
%% shift up to 24: xxxxxxxxyyyyyyyyzz000000
%%       or  0x2d: xxxxxxxxyyyyyyyyzz101101  (callibration)
%%  xor  with    : 111111111111111111111111
%%                 XXXXXXXXYYYYYYYYZZ010010
%%   inverted      xxxxxxxxyyyyyyyyzz1011011 (with start bit)
%% 
%%    sent like    1101101zzyyyyyyyyxxxxxxxx (left to right)
%%
encode_word(W) ->
    W1 = ((W bsl 6) bor 16#2d) bxor 16#ffffff, 
    <<W1:24/little>>.

