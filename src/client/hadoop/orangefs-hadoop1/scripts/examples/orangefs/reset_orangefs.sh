cd $(dirname $0)
./stop_orangefs.sh
sleep 1
./cleanup_orangefs.sh
sleep 1
./init_orangefs.sh
sleep 1
./start_orangefs.sh
