import inspect



def wordcount(testing_node,output=[]):

    # change directory

    testing_node.runSingleCommand("mkdir -p /home/%s/gutenberg" % testing_node.current_user)    
    testing_node.changeDirectory("/home/%s/gutenberg" % testing_node.current_user)    
    testing_node.runSingleCommand("%s/bin/hadoop dfs -mkdir /user/%s/gutenberg" % (testing_node.hadoop_location,testing_node.current_user))
    
    # get the gutenberg files
    testing_node.runSingleCommand("wget http://www.gutenberg.org/cache/epub/5000/pg5000.txt")
    testing_node.runSingleCommand("wget http://www.gutenberg.org/cache/epub/20417/pg20417.txt")
    testing_node.runSingleCommand("wget http://www.gutenberg.org/cache/epub/4300/pg4300.txt")
    
    testing_node.runSingleCommand("%s/bin/hadoop dfs -copyFromLocal /home/%s/gutenberg/* /user/%s/gutenberg" % (testing_node.hadoop_location,testing_node.current_user,testing_node.current_user))
    
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  wordcount /user/%s/gutenberg" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user),output)
    return rc

tests = [ wordcount
 ]
