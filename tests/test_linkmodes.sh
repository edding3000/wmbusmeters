#!/bin/bash

PROG="$1"

rm -rf testoutput
mkdir -p testoutput

TEST=testoutput

cat simulations/simulation_t1_and_c1.txt | grep '^{' > $TEST/test_expected.txt
$PROG --format=json --listento=c1,t1 simulations/simulation_t1_and_c1.txt \
      MyTapWater multical21:c1 76348799 "" \
      Wasser      apator162:t1   20202020 "" \
> $TEST/test_output.txt
if [ "$?" == "0" ]
then
    cat $TEST/test_output.txt | sed 's/"timestamp":"....-..-..T..:..:..Z"/"timestamp":"1111-11-11T11:11:11Z"/' > $TEST/test_responses.txt
    diff $TEST/test_expected.txt $TEST/test_responses.txt
    if [ "$?" == "0" ]
    then
        echo Linkmodes t1 and c1 OK
    fi
else
    echo Failure.
    exit 1
fi

MSG=$($PROG --listento=c1,t1 simulations/simulation_t1_and_c1.txt \
      MyTapWater multical21:c1 76348799 "" \
      Wasser      apator162:s1   20202020 "")

if [ "$MSG" != "(cmdline) cannot set link modes to: s1 because meter apator162 only transmits on: c1,t1" ]
then
    echo Failure. Did not expect: $MSG
    exit 1
fi

MSG=$($PROG --listento=c1,t1 simulations/simulation_t1_and_c1.txt \
      MyTapWater multical21:t1 76348799 "" \
      Wasser      apator162:c1   20202020 "")

if [ "$MSG" != "(cmdline) cannot set link modes to: t1 because meter multical21 only transmits on: c1" ]
then
    echo Failure. Did not expect: $MSG
    exit 1
fi

MSG=$($PROG --s1 simulations/simulation_t1_and_c1.txt \
      MyTapWater multical21:c1 76348799 "" \
      Wasser      apator162:t1   20202020 "" | tr -d ' \n')

CORRECT="(config)Youhavespecifiedtolistentothelinkmodes:s1butthemetersmighttransmiton:c1,t1(config)Thereforeyoumightmisstelegrams!Pleasespecifytheexpectedtransmitmodeforthemeters,eg:apator162:t1(config)Oruseadonglethatcanlistentoalltherequiredlinkmodesatthesametime."
if [ "$MSG" != "$CORRECT" ]
then
    echo Failure. Did not expect:
    echo $MSG
    exit 1
fi
