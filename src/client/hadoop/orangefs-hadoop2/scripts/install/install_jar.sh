set -x
cd $(dirname $0)
ORANGEFS_PREFIX="/opt/orangefs-denton.hadoop2.trunk"
THIS_DIR="$(pwd)"
PROJECT_DIR=$THIS_DIR/../../../../..
#ls $PROJECT_DIR
cd $PROJECT_DIR
#pwd
#ls
sudo cp target/orangefs-hadoop2-2.9.0.jar "${ORANGEFS_PREFIX}/lib/"
