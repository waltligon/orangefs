OrangeFS 2.9.0 and Bigtop (trunk) Integration Environment on CentOS 6



Prerequisites:
Java JDK 1.6/1.7, Apache Maven 3.0, (Apache Forrest 0.8, Apache Ant, git, subversion, autoconf, automake, liblzo2-dev, libz-dev, sharutils, libfuse-dev, libssl-dev)



Deployment steps:

1. Add Bigtop trunk repository:
sudo wget -O /etc/yum.repos.d/bigtop.repo http://bigtop01.cloudera.org:8080/view/Bigtop-trunk/job/Bigtop-trunk-repository/label=centos6/lastSuccessfulBuild/artifact/repo/bigtop.repo

2. Install the latest Hadoop stack provided in the Bigtop trunk repository:
sudo yum install hadoop\* flume\* mahout\* oozie\* whirr\* hbase\* spark\*

3. Switch to user hdfs and setup environment variables: 
export LD_LIBRARY_PATH=/opt/db4/lib:/opt/orangefs/lib
export JNI_LIBRARY_PATH=/opt/orangefs/lib
export PATH=${PATH}:/opt/orangefs/sbin:/opt/orangefs/bin
### BigTop ###
export JAVA_HOME=/usr/java/latest
export HADOOP_HOME=/usr/lib/hadoop
export HADOOP_CONF_DIR=/etc/hadoop/conf
export HBASE_HOME=/usr/lib/hbase
export HBASE_CONF_DIR=/etc/hbase/conf
export ZOOKEEPER_HOME=/usr/lib/zookeeper
export HIVE_HOME=/usr/lib/hive
export PIG_HOME=/usr/lib/pig
export FLUME_HOME=/usr/lib/flume
export SQOOP_HOME=/usr/lib/sqoop
export HCAT_HOME=/usr/lib/hcatalog
export OOZIE_URL=http://localhost:11000/oozie
export HADOOP_MAPRED_HOME=/usr/lib/hadoop-mapreduce
export HADOOP_CLASSPATH=${JNI_LIBRARY_PATH}/*:${HADOOP_CLASSPATH}

4. Deploy OrangeFS JNI shim layer:
http://www.orangefs.org/svn/orangefs/branches/denton.hadoop2.trunk/src/client/jni/README 

5. Compile and Deploy OrangeFS-Hadoop2 jar file:
http://www.orangefs.org/svn/orangefs/branches/denton.hadoop2.trunk/src/client/hadoop/orangefs-hadoop2/

6. Setup Hadoop configuration files: 
http://www.orangefs.org/svn/orangefs/branches/orangefs.bigtop.trunk/test/bigtop/bigtop-orangefs/conf/

7. Start OrangeFS server:
http://www.orangefs.org/svn/orangefs/branches/denton.hadoop2.trunk/src/client/hadoop/orangefs-hadoop2/scripts/examples/orangefs/ 

8. Start Hadoop services:
https://cwiki.apache.org/confluence/display/BIGTOP/How+to+install+Hadoop+distribution+from+Bigtop#HowtoinstallHadoopdistributionfromBigtop-RunningHadoop

9. Deploy OrangeFS-Bigtop trunk:
svn co http://www.orangefs.org/svn/orangefs/branches/orangefs.bigtop.trunk/test/bigtop/ 

10. Build tests:
mvn clean install -DskipTests -DskipITs -DperformRelease -f ./bigtop-test-framework/pom.xml
mvn clean install -DskipTests -DskipITs -DperformRelease -f ./bigtop-tests/test-artifacts/pom.xml
mvn clean install -DskipTests -DskipITs -DperformRelease -o -nsu -f ./bigtop-test-framework/pom.xml
mvn clean install -DskipTests -DskipITs -DperformRelease -o -nsu -f ./bigtop-tests/test-artifacts/pom.xml
mvn -f bigtop-tests/test-artifacts/pom.xml install
mvn -f bigtop-tests/test-execution/conf/pom.xml install
mvn -f bigtop-tests/test-execution/common/pom.xml install

11. Run smoke tests:
cd bigtop-tests/test-execution/smokes/hadoop
mvn verify
