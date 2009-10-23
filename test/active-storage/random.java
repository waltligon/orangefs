class Random
{
    public Random(String[] args)  
    {  
	int string_length = Integer.parseInt(args[0]);
	int file_size = Integer.parseInt(args[1]);
	int max_line = file_size;
	String tmp = args[2];

	//System.out.println("args[2]="+args[2]);
	//System.out.println("tmp="+tmp);

	if(tmp.matches("kb"))
	    max_line = max_line*1024/string_length;
	else if(tmp.matches("mb")) {
	    //System.out.println("MB???");
	    max_line = max_line*1024*1024/string_length;
	}
	else if(tmp.matches("gb"))
	    max_line = max_line*1024*1024*1024/string_length;
	else {
	    System.out.println("Error");
	    System.exit(1);
	}

	//System.out.println("max_line="+max_line);

	for (int i = 0; i < max_line; i++) {
	    StringBuffer sb = new StringBuffer();  
	    for (int x = 0; x < string_length-1; x++) {  
		sb.append((char)((int)(Math.random()*26)+97));  
	    }  
	    sb.append("\n");
	    System.out.print(sb.toString());  
	}
    }  
    public static void main(String[] args)
    {
	if(args.length != 3) {
	    System.err.println("Usage: java Random string_length(int) file_size(int) file_unit (kb|mb|gb)");
	    System.exit(1);
	}

	new Random(args);
    }  
}  