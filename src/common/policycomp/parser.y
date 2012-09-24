
%{

/*
 * (C) 2012 Clemson University and Omnibond, Inc.
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup policy
 *
 *  Parser for policy source-to-source translator.
 */

#include <stdlib.h>
#include <stdio.h>

#include <policyeval.h>
#include <policycomp.h>

#ifdef WIN32
#define _STDLIB_H  /* mark stdlib.h included */
#endif

/* We never use this, disable default. */
#define YY_LOCATION_PRINT 0

/* No NLS. */ 
#define YYENABLE_NLS 0

/* in scanner.l generated code */
extern int yylex(void);

/*
 * Local variables:
 *    c-indent-level: 4
 *    c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

%}

%union {
    int i;
    char *c;
};

%token <i> ATTRIBUTE
%token <i> POLICY
%token <i> FOR
%token <i> SETS
%token <i> SPREAD
%token <i> SELECT
%token <i> WHERE
%token <i> OTHERS
%token <i> LBRACE
%token <i> RBRACE
%token <i> LPAREN
%token <i> RPAREN
%token <i> SEMICOLON
%token <i> COMMA
%token <i> DASH
%token <c> IDENTIFIER
%token <c> INTEGER
%token <i> AND
%token <i> OR
%token <i> EQ
%token <i> NE
%token <i> GT
%token <i> GE
%token <i> LT
%token <i> LE

%type <i> cmpop set_criteria_count integer

%type <c> identifier cmpvalue num_sets

%start policy_set

%%

policy_set : {gen_initialize();}
             attr_set
            {gen_attrib_table();}
             policy_decl_set
            {gen_policy_table();
             gen_finalize();}
        ;

policy_decl_set : policy_decl
        | policy_decl policy_decl_set
        ;

attr_set : attr_decl
        | attr_decl attr_set
        ;

attr_decl : ATTRIBUTE identifier
            {gen_save_attr_name($2);}
            range_decl
        ;

range_decl : LPAREN
            {gen_begin_enum_decl();}
          value_list RPAREN SEMICOLON
            {gen_end_enum_decl();}
        | integer DASH integer SEMICOLON
            {gen_int_decl($1, $3);}
        ;

value_list : identifier
            {gen_value($1);}
        | identifier 
            {gen_value_comma($1);}
          COMMA value_list
        ;

policy_decl : policy_header policy_body
            {gen_inc_pnum();}
        ;

policy_header : POLICY identifier LPAREN policy_args RPAREN FOR IDENTIFIER
        ;

policy_args : /* NULL */
        | policy_arg comma_policy_args
        ;

comma_policy_args : /* NULL */
        | COMMA policy_arg comma_policy_args
        ;

policy_arg : type_id identifier
        ;

type_id : identifier
        ;

policy_body : LBRACE 
            {gen_init_join_criteria();}
                join_criteria_list
            {gen_end_join_criteria();
             gen_init_set_criteria();}
                set_decl
            {gen_end_set_criteria();}
            RBRACE
        ;

join_criteria_list : join_criteria
        | join_criteria {gen_jc_separator();} join_criteria_list
        ;

join_criteria : identifier EQ identifier SEMICOLON
            {gen_output_join_criteria($1,$3);}
        ;

set_decl : SETS num_sets spread_option set_body
        ;

num_sets : identifier
        | INTEGER
        | /* empty */ {$$ = NULL;}
        ;

spread_option : SPREAD identifier
            {gen_spread_attr($2);}
        | /* empty */
            {gen_spread_attr(NULL);}
        ;

set_body : LBRACE set_criteria_list RBRACE
        ;

set_criteria_list : set_criteria
        | set_criteria {gen_set_separator();} set_criteria_list
        ;

set_criteria : SELECT set_criteria_count 
            {gen_set_criteria_count($2);}
               set_criteria_expression
        ;

set_criteria_count : integer
            {$$ = $1;}
        | OTHERS
            {$$ = -1;}
        ;

set_criteria_expression :  WHERE 
            {gen_start_scfunc();}
            attr_expr SEMICOLON
            {gen_end_scfunc();}
        ;

attr_expr : attr_term OR {gen_or();} attr_expr
        | attr_term
        ;

attr_term : primary AND {gen_and();} attr_term
        | primary
        ;

primary : LPAREN {gen_lp();} attr_expr RPAREN {gen_rp();}
        | cmpare
        ;

cmpare : identifier cmpop cmpvalue
            {gen_compare($1, $2, $3);}
        ;

cmpvalue : identifier
        | INTEGER
        ;

cmpop :   EQ {$$ = SID_EQ;}
        | NE {$$ = SID_NE;}
        | GT {$$ = SID_GT;}
        | GE {$$ = SID_GE;}
        | LT {$$ = SID_LE;}
        | LE {$$ = SID_GE;}
        ;

identifier : IDENTIFIER {$$ = estrdup($1);}

integer : INTEGER {$$ = atoi($1);}

%%

