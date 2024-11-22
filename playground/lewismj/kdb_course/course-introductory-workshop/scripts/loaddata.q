loadtripsdb:{
    `KX_OBJSTR_INVENTORY_FILE setenv"_inventory/all.json.gz";
    system"l ",.trn.nbdir,"/data/taxi";
    0N!"Loaded Taxi Trips partitioned DB";
 };

loadweathercsv:{
    `weather set ("DFFFFFFFFF";enlist ",") 0: `$":",.trn.nbdir,"/data/weather.csv";
    0N!"Loaded Weather CSV";
 };

initdata:{[]
    0N!"Initializing variables";
    loadweathercsv[];
    / small sample to speed up load time, generated with: 1000#select from jan09;
    `smalltrips set ("DMSPPNIFFFFFSFEFFF";enlist ",") 0: `$":",.trn.nbdir,"/data/smalltrips.csv";
    loadtripsdb[];
 };

// Setup Temp Vars needed for later
initdata[]

0N!"Defining exercise results";
// Exercise 1
ex1_a:{select count i from trips where date = min date, passengers < 2};
ex1_b:{select count i from trips where date = max date, passengers < 2};

// Exercise 2
ex2:{select payment_type, fare from trips where date = min date};

// Exercise 3
ex3:{res2: select payment_type, fare from trips where date = min date};

// Exercise 4
ex4:{select maxTip:max tip, minTip:min tip from jan09};

// Exercise 5
ex5:{select max tip by vendor from jan09};

// Exercise 6
ex6:{select maxTip:max tip, avgTip:avg tip by payment_type from jan09};

// Exercise 7 parts a & b
ex7_a:{
    res7a:select avg tip by payment_type from jan09 where fare > (avg;fare) fby vendor;
    show res7a;
    select payment_type from res7a where tip = max tip};
ex7_b:{
    res7b:select count i by vendor from jan09 where duration < (avg;duration) fby vendor;
    show res7b;
    select vendor from res7b where x = max x};

// Exercise 8
ex8_a:{select max tip by 15 xbar pickup_time.minute from jan09};
ex8_b:{select max tip by 15 xbar pickup_time.minute,vendor from jan09};

// Exercise 9
ex9:{select max maxtemp, min mintemp by 7 xbar date from weather};

// Exercise 10
ex10:{jan09C lj `date xkey select date, avgtemp from weather };

//Exercise 11
ex11:{timetab:([] vendor: `VTS`DDS`CMT; pickup_time:3#2009.01.31D09:30:00);aj[`vendor`pickup_time;timetab;jan09]}

//Exercise 12
ex12:{
  t:([]sym:10#`aapl;time:09:30:01 09:30:03 09:30:06 09:30:08 09:30:12 09:30:13 09:30:16 09:30:17 09:30:20 09:30:22;price:100+10?100.);
 q:([] sym:18#`aapl;time:09:30:01+(til 5),(7+til 11),20+til 2;ask:98+18?100.;bid:95+18?100.);
 w:-3 1+\:t `time;
 wj[w;`sym`time;t;(q;(max;`ask);(min;`bid))]}

//Exercise 13
ex13:{-10 sublist 20 sublist fares}

//Exercise 14
ex14:{-10 sublist sortedFares}

//Exercise 15
ex15:{fares[10 + til 10]}

//Exercise 16
ex16:{sortedFares [`long$(count sortedFares)%2]}

//Exercise 17
ex17:{@[fares;where null fares;:;avg fares]}

//Exercise 18
exer18_a:{dict::`a`b`c!(3?10i;3?10i;3?10i);dict}
exer18_b:{dict[`d]:2*dict[`a];dict}
exer18_c:{tab::flip dict;tab}
exer18_d:{tab2::tab,tab;tab2}
exer18_e:{`b xkey tab2}
exer18_f:{(`dict`tab`tab2`tabk)!(99 98 98 99)}

//exercise 19
ex19:{neg (y*(1+x) xexp 2) %-1+2*1+x}

//exercise 20
ex20:{7.93*1.87}

//exercise 21
ex21_a:{[x;y] x%y}
ex21_b:{select tipPerDist:tipOverDistance[tip;distance], distance, tip, vendor from jan09 where distance > 0}
ex21_c:{select avg tipPerDist, avg distance, avg tip by vendor from createTable[]}

//exercise 22 - not used?
ex22:{speed30:speed[30];speed30 0.5 1}

//exercise 23
ex23:{x: 10 30 20 40;y: 13 34 25 46;x,'y}

//exercise 24
ex24_a:{{x+x}each 3 6 8}
ex24_b:{add2:{x+y};add2'[3 6 8;4 7 9]}
ex24_c:{(`scan`over)!({x*y}\[11;3 5 4 2];{x*y}/[11;3 5 4 2])}

0N!"Ready";
