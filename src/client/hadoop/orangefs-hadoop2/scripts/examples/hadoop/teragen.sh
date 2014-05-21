HADOOP_PREFIX=/opt/hadoop-2.2.0
HADOOP_CONFIG_DIR=/home/denton/projects/denton.hadoop2.trunk/src/client/hadoop/orangefs-hadoop2/src/main/resources/conf
${HADOOP_PREFIX}/bin/hadoop \
    --config ${HADOOP_CONFIG_DIR} \
    jar ${HADOOP_PREFIX}/share/hadoop/mapreduce/hadoop-mapreduce-examples-2.2.0.jar \
    teragen 1000000 teragen_data
