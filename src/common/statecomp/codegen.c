/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>

extern FILE *out_file;

void gen_init(void)
{
	fprintf(out_file,"\n#include <state-machine.h>\n");
}

void gen_state_decl(char *state_name)
{
	fprintf(out_file,"extern PINT_state_array_values ST_%s[];\n", state_name);
}

void gen_state_array(char *machine_name, char *first_state_name)
{
#if 0
	fprintf(out_file,"\nPINT_state_array_values %s[] = {\n", machine_name);
	fprintf(out_file,"(PINT_state_array_values)ST_%s\n};\n\n", first_state_name);
	fprintf(out_file,"\nextern PINT_state_array_values ST_%s[];\n",
				first_state_name);
#endif
	fprintf(out_file,"\nPINT_state_array_values *%s = ST_%s;\n\n",
				machine_name, first_state_name);
	fprintf(out_file, "#if 0\n");
	fprintf(out_file, "PINT_state_machine_s %s =\n{\n\t", machine_name);
	fprintf(out_file, "ST_%s,\n\t\"%s\"\n", first_state_name, machine_name);
	fprintf(out_file, "\t%s_init_state_machine\n};\n", machine_name);
	fprintf(out_file, "#endif\n\n");
}

void gen_state_start(char *state_name, int flag)
{
	fprintf(out_file,"static PINT_state_array_values ST_%s[] = {\n", state_name);
	fprintf(out_file,"(PINT_state_array_values)%d", flag);
}

void gen_state_run(char *run_func)
{
	fprintf(out_file,",\n(PINT_state_array_values)%s", run_func);
}

void gen_return_code(char *return_code)
{
	fprintf(out_file,",\n(PINT_state_array_values)%s", return_code);
}

void gen_state_return(void)
{
	fprintf(out_file,",\n(PINT_state_array_values)-1");
}

void gen_new_state(char *new_state)
{
	fprintf(out_file,",\n(PINT_state_array_values)ST_%s", new_state);
}

void gen_state_end(void)
{
	fprintf(out_file,"\n};\n\n");
}

