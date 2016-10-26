#if defined (__INTEL_COMPILER)
#pragma warning (disable:981)
#endif
#include "config.h"
#include "test_support.h"

#include <stdio.h>
#include <atl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include "evpath.h"
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#define drand48() (((double)rand())/((double)RAND_MAX))
#define lrand48() rand()
#define srand48(x)
#else
#include <sys/wait.h>
#endif

typedef struct _rec_a {
    int a_field;
    int sequence;
} rec_a, *rec_a_ptr;


static FMField a_field_list[] =
{
    {"a_field", "integer",
     sizeof(int), FMOffset(rec_a_ptr, a_field)},
    {"sequence", "integer",
     sizeof(int), FMOffset(rec_a_ptr, sequence)},
    {NULL, NULL, 0, 0}
};

static FMStructDescRec a_format_list[] =
{
    {"a_rec", a_field_list, sizeof(rec_a), NULL},
    {NULL, NULL, 0, NULL}
};

static FMStructDescList queue_list[] = {a_format_list, NULL};

static char * store_func = "{\n\
    int found = 0;\n\
    a_rec *a;\n\
    if (EVcount_a_rec() > 0) {\n\
	printf(\"Got the event in the storage stone!\\n\");\n\
	EVdiscard_and_submit_a_rec(0, 0);\n\
    }\n\
}\0\0";

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    /*
    simple_rec_ptr event = vevent;
    long sum = 0, scan_sum = 0;
    (void)cm;
    sum += event->integer_field % 100;
    sum += event->short_field % 100;
    sum += event->long_field % 100;
    sum += ((int) (event->nested_field.item.r * 100.0)) % 100;
    sum += ((int) (event->nested_field.item.i * 100.0)) % 100;
    sum += ((int) (event->double_field * 100.0)) % 100;
    sum += event->char_field;
    sum = sum % 100;
    scan_sum = event->scan_sum;
    if (sum != scan_sum) {
	printf("Received record checksum does not match. expected %d, got %d\n",
	       (int) sum, (int) scan_sum);
    }
    if ((quiet <= 0) || (sum != scan_sum)) {
	printf("In the handler, event data is :\n");
	printf("	integer_field = %d\n", event->integer_field);
	printf("	short_field = %d\n", event->short_field);
	printf("	long_field = %ld\n", event->long_field);
	printf("	double_field = %g\n", event->double_field);
	printf("	char_field = %c\n", event->char_field);
	printf("Data was received with attributes : \n");
	if (attrs) dump_attr_list(attrs);
    }
    if (client_data != NULL) {
	int tmp = *((int *) client_data);
	*((int *) client_data) = tmp + 1;
    }
    */

    printf("Received message!\n");
    return 0;
}

//static int do_regression_master_test(int do_dll);
//static int repeat_count = 10;
static atom_t CM_TRANSPORT;
static atom_t CM_NETWORK_POSTFIX;
static atom_t CM_MCAST_ADDR;
static atom_t CM_MCAST_PORT;

char *transport = NULL;

extern int
be_test_receiver(int argc, char **argv)
{
    CManager cm;
    srand48(getpid());
    CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
    CM_NETWORK_POSTFIX = attr_atom_from_string("CM_NETWORK_POSTFIX");
    CM_MCAST_PORT = attr_atom_from_string("MCAST_PORT");
    CM_MCAST_ADDR = attr_atom_from_string("MCAST_ADDR");
    
    cm = CManager_create();

    attr_list contact_list, listen_list = NULL;
    char *postfix = NULL;
    char *string_list;
    EVstone term;
    if (!transport) transport = getenv("CMTransport");
    if (transport != NULL) {
        if (listen_list == NULL) listen_list = create_attr_list();
        add_attr(listen_list, CM_TRANSPORT, Attr_String,
    	     (attr_value) strdup(transport));
    }
    if ((postfix = getenv("CMNetworkPostfix")) != NULL) {
        if (listen_list == NULL) listen_list = create_attr_list();
        add_attr(listen_list, CM_NETWORK_POSTFIX, Attr_String,
    	     (attr_value) strdup(postfix));
    }
    CMlisten_specific(cm, listen_list);
    contact_list = CMget_contact_list(cm);

    if (contact_list) {
		string_list = attr_list_to_string(contact_list);
    }
    else {
		fprintf(stderr, "Error: contact_list not initialized correctly\n");
		return 1;
    }

    term = EValloc_stone(cm);
    EVassoc_terminal_action(cm, term, a_format_list, simple_handler, NULL);

    char *full_list[2] = { string_list, NULL};
    
    test_fork_child(&term, full_list);
    
    CMsleep(cm, 3);
    return 0;

}

extern int
be_test_sender(int argc, char **argv) {
	
	if(argc < 1) {
		fprintf(stderr, "Error: not enough arguments for sender!!\n");
		return 1;
	}

	CManager    cm;
	EVstone	    bridge, remote, store;
	EVsource    data_source;
	EVaction    store_action;
	attr_list   contact_list;
	rec_a data;
	char	    string_list[2048];
	char *	    storage_spec;
	//int	    test_group = 3; 

	if(sscanf(argv[0], "%d:%s", &remote, string_list) != 2) {
		fprintf(stderr, "Error: could not read arg: %s\n", argv[0]);
		return 1;
	}

	cm = CManager_create();
	// Do I need the following line?  I don't think so....
	CMlisten(cm);

	bridge = EValloc_stone(cm);
	contact_list = attr_list_from_string(string_list);
	EVassoc_bridge_action(cm, bridge, contact_list, remote);


	store = EValloc_stone(cm);
	storage_spec = create_storage_action_spec(queue_list, store_func);
	//printf("Value of the storagespec:\n%s\n", storage_spec);
	store_action = EVassoc_multi_action(cm, store, storage_spec, NULL);
	EVaction_set_output(cm, store, store_action, 0, bridge);

	//printf("Shouldn't make it here!\n");
	//exit(1);
	
	

	data_source = EVcreate_submit_handle(cm, store, a_format_list);
	memset(&data, 0, sizeof(data));

	if (quiet <=0) {printf("submitting %d\n", data.a_field);}
	EVsubmit(data_source, &data, NULL);

	return 0;
	
}
	
