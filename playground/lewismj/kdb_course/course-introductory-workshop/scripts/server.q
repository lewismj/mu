/ sync queries
populations:([borough:`Bronx`Brooklyn`Manhattan`Queens`StatenIsland]
              population:1471160 2648771 1664727 2358582 479458)

/ pub/sub
.srv.subs:0#0i
sub:{[].srv.subs:.srv.subs union .z.w;}
.z.pc:{.srv.subs:.srv.subs except x;}
.srv.upd:{@[{upd x};x;{}]}

.srv.vendors:`AAA`BBB`CCC / need full list
apprequests:([]time:"n"$();vendor:`$();lat:"f"$();long:"f"$())

.srv.createrequest:{[]first apprequests upsert(.z.N;rand .srv.vendors;40.57366+rand 0.4681;-74.20012+ rand 0.62768)}

.srv.sendrequest:{[h;x]neg[h](.srv.upd;x)}
.srv.pub:{.srv.sendrequest[;.srv.createrequest[]]each .srv.subs;}

.srv.freq:.0001
.srv.chk:{.srv.freq>rand .1}

.z.ts:{if[.srv.chk[];.srv.pub[]];}
\t 1
