/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <../../server/state-comp.h>

extern FILE *out_file;

void gen_init(void)
{
	fprintf(out_file,"\n#include <state-machine.h>\n");
}

void gen_state_decl(char *state_name)
{
	fprintf(out_file,"extern PINT_state_array_values ST_%s[];\n", state_name);
}

void gen_machine(char *machine_name, char *first_state_name, char *init_name)
{
	fprintf(out_file, "\nPINT_state_machine_s %s =\n{\n\t", machine_name);
	fprintf(out_file, "ST_%s,\n\t\"%s\",\n", first_state_name, machine_name);
	if (init_name)
	{
		fprintf(out_file, "\t%s\n};\n\n", init_name);
	}
	else
	{
		fprintf(out_file, "\tNULL\n};\n\n");
	}
}

void gen_state_start(char *state_name)
{
	fprintf(out_file,"static PINT_state_array_values ST_%s[] = {\n", state_name);
}

void gen_state_action(char *run_func, int flag)
{
	fprintf(out_file,"(PINT_state_array_values)%d", flag);
	fprintf(out_file,",\n(PINT_state_array_values)%s", run_func);
}

void gen_return_code(char *return_code)
{
	fprintf(out_file,",\n(PINT_state_array_values)%s", return_code);
}

void gen_next_state(int flag, char *new_state)
{
	if (flag == SM_NEXT)
	{
		fprintf(out_file,",\n(PINT_state_array_values)ST_%s", new_state);
	}
	else
	{
		fprintf(out_file,",\n(PINT_state_array_values)%d", flag);
	}
}

void gen_state_end(void)
{
	fprintf(out_file,"\n};\n\n");
}

