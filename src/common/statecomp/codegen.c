/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>

extern FILE *out_file;

gen_init()
{
	fprintf(out_file,"\n#include <state_machine.h>\n");
}

gen_state_decl(char *state_name)
{
	fprintf(out_file,"extern PINT_state_array_values ST_%s[];\n", state_name);
}

gen_state_array(char *machine_name, char *first_state_name)
{
	fprintf(out_file,"\nPINT_state_array_values %s[] = {\n", machine_name);
	fprintf(out_file,"(PINT_state_array_values)ST_%s\n};\n\n", first_state_name);
}

gen_state_start(char *state_name)
{
	fprintf(out_file,"static PINT_state_array_values ST_%s[] = {\n", state_name);
}

gen_state_run(char *run_func)
{
	fprintf(out_file,"(PINT_state_array_values)%s", run_func);
}

gen_return_code(char *return_code)
{
	fprintf(out_file,",\n(PINT_state_array_values)%s", return_code);
}

gen_new_state(char *new_state)
{
	fprintf(out_file,",\n(PINT_state_array_values)ST_%s", new_state);
}

gen_state_end()
{
	fprintf(out_file,"\n};\n\n");
}

