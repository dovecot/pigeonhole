#include "lib.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "sieve-code.h"

#include <stdio.h>

/* 
 * Coded stringlist
 */

struct sieve_coded_stringlist {
  struct sieve_binary *binary;
  sieve_size_t start_address;
  sieve_size_t end_address;
  sieve_size_t current_offset;
  int length;
  int index;
};

bool sieve_coded_stringlist_next_item(struct sieve_coded_stringlist *strlist, string_t **str) 
{
	sieve_size_t address;
  *str = NULL;
  
  if ( strlist->index >= strlist->length ) 
    return TRUE;
  else {
  	address = strlist->current_offset;
  	
  	if ( sieve_binary_read_string(strlist->binary, &address, str) ) {
    	strlist->index++;
    	strlist->current_offset = address;
    	return TRUE;
    }
  }  
  
  return FALSE;
}

void sieve_coded_stringlist_reset(struct sieve_coded_stringlist *strlist) 
{  
  strlist->current_offset = strlist->start_address;
  strlist->index = 0;
}

inline int sieve_coded_stringlist_get_length(struct sieve_coded_stringlist *strlist)
{
	return strlist->length;
}

inline sieve_size_t sieve_coded_stringlist_get_end_address(struct sieve_coded_stringlist *strlist)
{
	return strlist->end_address;
}

inline sieve_size_t sieve_coded_stringlist_get_current_offset(struct sieve_coded_stringlist *strlist)
{
	return strlist->current_offset;
}

/*
 * Operand functions
 */
 
inline sieve_size_t sieve_operand_emit_code
	(struct sieve_binary *sbin, int operand)
{
	unsigned char op = operand & SIEVE_OPERAND_CORE_MASK;
	
	return sieve_binary_emit_byte(sbin, op);
}
 
const struct sieve_operand *sieve_operand_read
	(struct sieve_binary *sbin, sieve_size_t *address) 
{
	unsigned int operand;
	
	if ( sieve_binary_read_byte(sbin, address, &operand) ) {
		if ( operand < SIEVE_OPERAND_EXT_OFFSET ) {
			if ( operand < sieve_operand_count )
				return sieve_operands[operand];
			else
				return NULL;
		} else {
		  const struct sieve_extension *ext = 
		  	sieve_binary_get_extension(sbin, operand - SIEVE_OPERAND_EXT_OFFSET);
		  
		  if ( ext != NULL )
		  	return ext->operand;	
		  else
		  	return NULL;
		}
	}		
	
	return NULL;
}

/* 
 * Operand definitions
 */
 
/* Number */

static bool opr_number_dump(struct sieve_binary *sbin, sieve_size_t *address);
static bool opr_number_read(struct sieve_binary *sbin, sieve_size_t *address, sieve_size_t *number);

const struct sieve_opr_number_interface number_interface = { 
	opr_number_dump, 
	opr_number_read
};

const struct sieve_operand_class number_class = 
	{ "number", &number_interface };
	
const struct sieve_operand number_operand = 
	{ "number", &number_class };

/* String */

static bool opr_string_read
  (struct sieve_binary *sbin, sieve_size_t *address, string_t **str);
static bool opr_string_dump(struct sieve_binary *sbin, sieve_size_t *address);

const struct sieve_opr_string_interface string_interface ={ 
	opr_string_dump, 
	opr_string_read
};
	
const struct sieve_operand_class string_class = 
	{ "string", &string_interface };
	
const struct sieve_operand string_operand = 
	{ "string", &string_class };
	

/* String List */

static bool opr_stringlist_dump
	(struct sieve_binary *sbin, sieve_size_t *address);
static struct sieve_coded_stringlist *opr_stringlist_read
	(struct sieve_binary *sbin, sieve_size_t *address, bool single);

const struct sieve_opr_stringlist_interface stringlist_interface = { 
	opr_stringlist_dump, 
	opr_stringlist_read
};
	
const struct sieve_operand_class stringlist_class = 
	{ "string-list", &stringlist_interface };

const struct sieve_operand stringlist_operand = 
	{ "string-list", &stringlist_class };
	
/* Core operands */

const struct sieve_operand *sieve_operands[] = {
	&number_operand,
	&string_operand,
	&stringlist_operand
}; 

const unsigned int sieve_operand_count =
	(sizeof(sieve_operands) / sizeof(sieve_operands[0]));
	
/* 
 * Operand implementations 
 */
 
/* Number */

void sieve_opr_number_emit(struct sieve_binary *sbin, sieve_size_t number) 
{
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_NUMBER);
  (void) sieve_binary_emit_integer(sbin, number);
}

bool sieve_opr_number_dump(struct sieve_binary *sbin, sieve_size_t *address) 
{
	sieve_size_t pc = *address;
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	const struct sieve_opr_number_interface *intf;
	
	printf("%08x:   ", pc);
	
	if ( operand == NULL || operand->class != &number_class ) 
		return FALSE;
		
	intf = (const struct sieve_opr_number_interface *) operand->class->interface; 
	
	if ( intf->dump == NULL )
		return FALSE;

	return intf->dump(sbin, address);  
}

bool sieve_opr_number_read
  (struct sieve_binary *sbin, sieve_size_t *address, sieve_size_t *number)
{
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	const struct sieve_opr_number_interface *intf;
	
	if ( operand == NULL || operand->class != &number_class ) 
		return FALSE;
		
	intf = (const struct sieve_opr_number_interface *) operand->class->interface; 
	
	if ( intf->dump == NULL )
		return FALSE;

	return intf->read(sbin, address, number);  
}

static bool opr_number_dump(struct sieve_binary *sbin, sieve_size_t *address) 
{
	sieve_size_t number = 0;
	
	if (sieve_binary_read_integer(sbin, address, &number) ) {
	  printf("NUM: %ld\n", (long) number);		
	  
	  return TRUE;
	}
	
	return FALSE;
}

static bool opr_number_read
  (struct sieve_binary *sbin, sieve_size_t *address, sieve_size_t *number)
{ 
	return sieve_binary_read_integer(sbin, address, number);
}

/* String */

void sieve_opr_string_emit(struct sieve_binary *sbin, string_t *str)
{
	(void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_STRING);
  (void) sieve_binary_emit_string(sbin, str);
}

bool sieve_opr_string_dump(struct sieve_binary *sbin, sieve_size_t *address) 
{
	sieve_size_t pc = *address;
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	const struct sieve_opr_string_interface *intf;
	
	printf("%08x:   ", pc);
	
	if ( operand == NULL || operand->class != &string_class ) 
		return FALSE;
		
	intf = (const struct sieve_opr_string_interface *) operand->class->interface; 
	
	if ( intf->dump == NULL ) 
		return FALSE;

	return intf->dump(sbin, address);  
}

bool sieve_opr_string_read
  (struct sieve_binary *sbin, sieve_size_t *address, string_t **str)
{
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	const struct sieve_opr_string_interface *intf;
	
	if ( operand == NULL || operand->class != &string_class ) 
		return FALSE;
		
	intf = (const struct sieve_opr_string_interface *) operand->class->interface; 
	
	if ( intf->dump == NULL )
		return FALSE;

	return intf->read(sbin, address, str);  
}


static void _print_string(string_t *str) 
{
	unsigned int i = 0;
  const unsigned char *sdata = str_data(str);

	printf("STR[%ld]: \"", (long) str_len(str));

	while ( i < 40 && i < str_len(str) ) {
	  if ( sdata[i] > 31 ) 
	    printf("%c", sdata[i]);
	  else
	    printf(".");
	    
	  i++;
	}
	
	if ( str_len(str) < 40 ) 
	  printf("\"\n");
	else
	  printf("...\n");
}

bool opr_string_dump(struct sieve_binary *sbin, sieve_size_t *address) 
{
	string_t *str; 
	
	if ( sieve_binary_read_string(sbin, address, &str) ) {
		_print_string(str);   		
		
		return TRUE;
  }
  
  return FALSE;
}

static bool opr_string_read
  (struct sieve_binary *sbin, sieve_size_t *address, string_t **str)
{ 	
	return sieve_binary_read_string(sbin, address, str);
}

/* String list */

void sieve_opr_stringlist_emit_start
	(struct sieve_binary *sbin, unsigned int listlen, void **context)
{
  sieve_size_t *end_offset = t_new(sieve_size_t, 1);

	/* Emit byte identifying the type of operand */	  
  (void) sieve_operand_emit_code(sbin, SIEVE_OPERAND_STRING_LIST);
  
  /* Give the interpreter an easy way to skip over this string list */
  *end_offset = sieve_binary_emit_offset(sbin, 0);
	*context = (void *) end_offset;

  /* Emit the length of the list */
  (void) sieve_binary_emit_integer(sbin, (int) listlen);
}

void sieve_opr_stringlist_emit_item
	(struct sieve_binary *sbin, void *context ATTR_UNUSED, string_t *item)
{
	(void) sieve_binary_emit_string(sbin, item);
}

void sieve_opr_stringlist_emit_end
	(struct sieve_binary *sbin, void *context)
{
	sieve_size_t *end_offset = (sieve_size_t *) context;

	(void) sieve_binary_resolve_offset(sbin, *end_offset);
}

bool sieve_opr_stringlist_dump(struct sieve_binary *sbin, sieve_size_t *address) 
{
	sieve_size_t pc = *address;
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	
	printf("%08x:   ", pc);
	
	if ( operand == NULL )
		return FALSE;
	
	if ( operand->class == &stringlist_class ) {
		const struct sieve_opr_stringlist_interface *intf =
			(const struct sieve_opr_stringlist_interface *) operand->class->interface; 
		
		if ( intf->dump == NULL )
			return FALSE;

		return intf->dump(sbin, address); 
	} else if ( operand->class == &string_class ) {
		const struct sieve_opr_string_interface *intf =
			(const struct sieve_opr_string_interface *) operand->class->interface; 
	
		if ( intf->dump == NULL ) 
			return FALSE;

		return intf->dump(sbin, address);  
	}
	
	return FALSE;
}

struct sieve_coded_stringlist *sieve_opr_stringlist_read
  (struct sieve_binary *sbin, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);
	const struct sieve_opr_stringlist_interface *intf;
	
	if ( operand == NULL)
		return NULL;
		
	if ( operand->class == &stringlist_class )  	
		intf = (const struct sieve_opr_stringlist_interface *) operand->class->interface; 
	else if ( operand == &string_operand ) 
		intf = (const struct sieve_opr_stringlist_interface *) stringlist_class.interface; 
	else
		return NULL;
		
	if ( intf->read == NULL ) 
		return NULL;

	return intf->read(sbin, address, operand == &string_operand);  
}

static bool opr_stringlist_dump
	(struct sieve_binary *sbin, sieve_size_t *address) 
{
	struct sieve_coded_stringlist *strlist;
	
  if ( (strlist=opr_stringlist_read(sbin, address, FALSE)) != NULL ) {
  	sieve_size_t pc;
		string_t *stritem;
		
		printf("STRLIST [%d] (END %08x)\n", 
			sieve_coded_stringlist_get_length(strlist), 
			sieve_coded_stringlist_get_end_address(strlist));
	  	
	 	pc = sieve_coded_stringlist_get_current_offset(strlist);
		while ( sieve_coded_stringlist_next_item(strlist, &stritem) && stritem != NULL ) {	
			printf("%08x:      ", pc);
			_print_string(stritem);
			pc = sieve_coded_stringlist_get_current_offset(strlist);  
		}
		
		return TRUE;
	}
	
	return FALSE;
}

static struct sieve_coded_stringlist *opr_stringlist_read
  (struct sieve_binary *sbin, sieve_size_t *address, bool single)
{
	struct sieve_coded_stringlist *strlist;

  sieve_size_t pc = *address;
  sieve_size_t end; 
  sieve_size_t length = 0; 
 
 	if ( single ) {
 		sieve_size_t strlen;
 		
 		if ( !sieve_binary_read_integer(sbin, address, &strlen) ) 
    	return NULL;

    end = *address + strlen;
    length = 1;
    *address = pc;
	} else {
		int end_offset;
		
  	if ( !sieve_binary_read_offset(sbin, address, &end_offset) )
  		return NULL;
  
  	end = pc + end_offset;
  
  	if ( !sieve_binary_read_integer(sbin, address, &length) ) 
    	return NULL;
  }
  
  if ( end > sieve_binary_get_code_size(sbin) ) 
  		return NULL;
    
	strlist = p_new(pool_datastack_create(), struct sieve_coded_stringlist, 1);
	strlist->binary = sbin;
	strlist->start_address = *address;
	strlist->current_offset = *address;
	strlist->end_address = end;
	strlist->length = length;
	strlist->index = 0;
  
  /* Skip over the string list for now */
  *address = end;
  
  return strlist;
}

/* 
 * Opcodes
 */

/* Opcode functions */

inline sieve_size_t sieve_operation_emit_code
	(struct sieve_binary *sbin, int opcode)
{
	unsigned char op = opcode & SIEVE_OPCODE_CORE_MASK;
	
	return sieve_binary_emit_byte(sbin, op);
}

inline sieve_size_t sieve_operation_emit_code_ext
	(struct sieve_binary *sbin, const struct sieve_extension *extension)
{	
	unsigned char op = SIEVE_OPCODE_EXT_OFFSET + 
		sieve_binary_get_extension_index(sbin, extension);
	
	return sieve_binary_emit_byte(sbin, op);
}

const struct sieve_opcode *sieve_operation_read
	(struct sieve_binary *sbin, sieve_size_t *address) 
{
	unsigned int opcode;
	
	if ( sieve_binary_read_byte(sbin, address, &opcode) ) {
		if ( opcode < SIEVE_OPCODE_EXT_OFFSET ) {
			if ( opcode < sieve_opcode_count )
				return sieve_opcodes[opcode];
			else
				return NULL;
		} else {
		  const struct sieve_extension *ext = 
		  	sieve_binary_get_extension(sbin, opcode - SIEVE_OPCODE_EXT_OFFSET);
		  
		  if ( ext != NULL )
		  	return ext->opcode;	
		  else
		  	return NULL;
		}
	}		
	
	return NULL;
}

static bool opc_jmp_dump(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool opc_jmptrue_dump(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool opc_jmpfalse_dump(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool opc_stop_dump(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool	opc_keep_dump(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool opc_discard_dump(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);

static bool opc_jmp_execute(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool opc_jmptrue_execute(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool opc_jmpfalse_execute(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool opc_stop_execute(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool opc_keep_execute(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool opc_discard_execute(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);

const struct sieve_opcode jmp_opcode = { opc_jmp_dump, opc_jmp_execute };
const struct sieve_opcode jmptrue_opcode = { opc_jmptrue_dump, opc_jmptrue_execute };
const struct sieve_opcode jmpfalse_opcode = { opc_jmpfalse_dump, opc_jmpfalse_execute };
const struct sieve_opcode stop_opcode = { opc_stop_dump, opc_stop_execute };
const struct sieve_opcode keep_opcode = { opc_keep_dump, opc_keep_execute };
const struct sieve_opcode discard_opcode = { opc_discard_dump, opc_discard_execute };

extern const struct sieve_opcode tst_address_opcode;
extern const struct sieve_opcode tst_header_opcode;
extern const struct sieve_opcode tst_exists_opcode;
extern const struct sieve_opcode tst_size_over_opcode;
extern const struct sieve_opcode tst_size_under_opcode;

const struct sieve_opcode *sieve_opcodes[] = {
  &jmp_opcode,
  &jmptrue_opcode, 
  &jmpfalse_opcode,
  &stop_opcode,
  &keep_opcode,
  &discard_opcode,

  &tst_address_opcode,
  &tst_header_opcode,
  &tst_exists_opcode,
  &tst_size_over_opcode,
  &tst_size_under_opcode
}; 

const unsigned int sieve_opcode_count =
	(sizeof(sieve_opcodes) / sizeof(sieve_opcodes[0]));

/* Code dump for core commands */

static bool opc_jmp_dump(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	unsigned int pc = *address;
	int offset;
	
	if ( sieve_binary_read_offset(sbin, address, &offset) ) 
		printf("JMP %d [%08x]\n", offset, pc + offset);
	else
		return FALSE;
	
	return TRUE;
}	
		
static bool opc_jmptrue_dump(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{	
	unsigned int pc = *address;
	int offset;
	
	if ( sieve_binary_read_offset(sbin, address, &offset) ) 
		printf("JMPTRUE %d [%08x]\n", offset, pc + offset);
	else
		return FALSE;
	
	return TRUE;
}

static bool opc_jmpfalse_dump(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{	
	unsigned int pc = *address;
	int offset;
	
	if ( sieve_binary_read_offset(sbin, address, &offset) ) 
		printf("JMPFALSE %d [%08x]\n", offset, pc + offset);
	else
		return FALSE;
	
	return TRUE;
}	
	
static bool opc_stop_dump(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	printf("STOP\n");
	
	return TRUE;
}

static bool opc_keep_dump(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	printf("KEEP\n");
	
	return TRUE;
}

static bool opc_discard_dump(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	printf("DISCARD\n");
	
	return TRUE;
}

/* Code execution for core commands */

static bool opc_jmp_execute(
	struct sieve_interpreter *interp, 
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED) 
{
	printf("JMP\n");
	if ( !sieve_interpreter_program_jump(interp, TRUE) )
		return FALSE;
	
	return TRUE;
}	
		
static bool opc_jmptrue_execute(
	struct sieve_interpreter *interp, 
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	if ( !sieve_interpreter_program_jump(interp,
		sieve_interpreter_get_test_result(interp)) )
		return FALSE;
		
	printf("JMPTRUE\n");
	
	return TRUE;
}

static bool opc_jmpfalse_execute(
	struct sieve_interpreter *interp, 
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	if ( !sieve_interpreter_program_jump(interp,
		!sieve_interpreter_get_test_result(interp)) )
		return FALSE;
		
	printf("JMPFALSE\n");
	
	return TRUE;
}	
	
static bool opc_stop_execute(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	printf(">> STOP\n");
	
	return FALSE;
}

static bool opc_keep_execute(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	printf(">> KEEP\n");
	
	return TRUE;
}

static bool opc_discard_execute(
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	printf(">> DISCARD\n");
	
	return TRUE;
}

