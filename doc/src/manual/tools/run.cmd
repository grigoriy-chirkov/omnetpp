call ..\..\..\..\setenv.bat
perl stripgrammar.pl ../../../../src/nedxml/ned2.y >ned.txt
perl stripgrammar.pl ../../../../src/nedxml/msg2.y >msg.txt
perl stripeventlog.pl ../../../../src/eventlog/eventlogentries.txt >eventlog.txt
opp_run -h nedfunctions >tmp.txt
perl processnedfuncs.pl <tmp.txt >nedfuncs.tex
del tmp.txt