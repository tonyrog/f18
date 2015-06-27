%%% Erlang version of add_stream2
-module(add_stream2).
-compile(export_all).

start() ->
    Run = 
	fun Run([]) ->
		receive
		    F when is_function(F) ->
			Pid = spawn(fun() -> Run([]) end),
			Run([Pid|F]);
		    _ -> %% drop until we get a function
			Run([])
		end;
	    Run(PF=[Pid|F]) ->
		receive
		    X when is_number(X) -> Pid ! F(X), Run(PF);
		    G when is_function(G) -> Pid ! G, Run(PF);
		    eof -> Pid ! eof, ok
		end
	end,
    Pid = spawn(fun() -> Run([]) end),
    Pid ! fun (X) -> X + 1 end,
    Pid ! fun (X) -> 2*X end,
    Pid ! fun (X) -> io:format("~w\n", [X]) end,
    lists:foreach(fun(I) -> Pid ! I end, lists:seq(3, 11, 2)),
    Pid ! 1,
    lists:foreach(fun(I) -> Pid ! I end, lists:seq(2, 10, 2)),
    Pid ! eof,
    ok.

