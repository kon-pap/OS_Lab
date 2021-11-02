#!/bin/bash

#Challenge 0
touch .hello_there;

#Challenge 1
chmod 444 .hello_there;

#Challenge 3
export ANSWER=42;

#Challenge 4
mkfifo magic_mirror && chmod 666 magic_mirror;

#Challenge 5
exec 99<&1;

#Challenge 6
mkfifo pipe1 && exec 33<>pipe1 && exec 34<>pipe1;
mkfifo pipe2 && exec 53<>pipe2 && exec 54<>pipe2;

#Challenge 7
ln .hello_there .hey_there;

#Challenge 8
exec 2>/dev/null;
for n in $(seq 0 9)
do
        filename=$(echo bf0$(echo $n));
        echo "Y" > tmp;
        dd if="./tmp" of=$filename bs=1 count=1 seek=1073741824;
        rm tmp;
done

exec 2>/dev/tty;

#Challenge 9 - Part 1
#netcat -d -l 49842 1>listener &
#netcat_job_id=$!;
# gcc server.c -o server;
# gcc ch12memwriter.c -o ch12memwriter;
make;
sleep 1s;
./server & 




#Challenge 10
sleep 2s;
touch secret_number;
mkfifo intermediary
exec 3<secret_number
exec 10<>intermediary
##HERE LIES THE ONLY ./riddle CALL
sleep 1s;
intermediary_2="intermediary_2"; #challenge 12 solver prepper
exec &> >(tee -a "$intermediary_2");
sudo echo "32766" > /proc/sys/kernel/ns_last_pid; #challenge 14 solver
./riddle <&10 &
##... anticlimactic undercomment
riddle_job_id=$!;
sleep 1s;
kill -SIGCONT $riddle_job_id;
sleep 3s;
read -u 3 secret;
number=$(echo $secret | cut -d ' ' -f 12);
touch secret_number; #re-create while it waits for the first esp input
exec 3<secret_number; #re-open too
echo $number >intermediary;
# echo $pid
# ps -e | grep -e "riddle" | awk '{print $1}' | xargs kill -SIGKILL
# wait
# rm intermediary
# echo "------------------------------------------------"

#Challenge 2
# ./riddle &
# riddle_job_id=$!;
# sleep 1s;
# kill -SIGCONT $riddle_job_id;

#Challenge 11
# touch secret_number;
# mkfifo intermediary
# exec 3<secret_number
# exec 10<>intermediary
# ./riddle <&10  &
sleep 3s;
read -u 3 secret;
number=$(echo $secret | cut -d ' ' -f 12);
pid=$(echo $(ps -A | grep -e "riddle" | awk '{print $1}'));
echo $number >intermediary;
# echo $pid
# sleep 1s
# ps -e | grep -e "riddle" | awk '{print $1}' | xargs kill -SIGKILL
# wait
# echo "------------------------------------------------"

chmod 777 .hello_there; #challenge 13 prepper

#Challenge 12
# ./riddle 2>intermediary_2 &
sleep 1s;
line=$(grep 'I want to find' intermediary_2);
letter=$(echo $line | cut -d ' ' -f 7 | cut -d "'" -f 2);
memplace=$(echo $line | cut -d ' ' -f 9 | cut -d 'x' -f 2);
# echo $pid $memplace $letter;
sleep 2s;
# cd /tmp;
# file=$(ls | grep -e "riddle*");
# printf $letter | dd of=$memplace bs=1 seek=111 count=1;
# pid=$(ps -e | grep -e "riddle" | awk '{print $1}')
./ch12memwriter $pid $memplace $letter;
# #ps -e | grep -e "riddle" | awk '{print $1}' | xargs kill -SIGCONT
# #wait
# echo "------------------------------------------------"

#Challenge 13
# mkfifo piper
# exec 10<>piper
# ./riddle <&10 &
sleep 2s;
truncate -s 32768 .hello_there;
# echo $pid;
sleep 2s;
echo "" >intermediary;
sleep 1s
# ps -e | grep -e "riddle" | awk '{print $1}' | xargs kill -SIGKILL
# wait
# echo "------------------------------------------------"

# #Challenge 14
# ./riddle

#Clear
echo "Clearing everything"
rm -f riddle.savegame .hello_there .hey_there magic_mirror pipe1 pipe2 bf0* filename secret listener intermediary intermediary_2
