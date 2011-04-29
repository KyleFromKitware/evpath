#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "evpath.h"
#include "ev_dfg.h"
#include "test_support.h"

static int status;
static EVdfg test_dfg;
const int reconfig_node_count = 10;

char *str_contact;
char **reconfig_list = NULL;

CManager cm;

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    //printf("\nMarker to start reconfiguration.. All the best! count = %d\n", count);
    simple_rec_ptr event = vevent;
    (void)cm;
    (void)client_data;
    static int count;
    checksum_simple_record(event, attrs, quiet);
    if (++count == 300) {
		EVdfg_shutdown(test_dfg, 0);
    }
    printf("\nMarker to start reconfiguration.. All the best! count = %d\n", count);
    return 0;
}

static int node_count = 2;
static EVdfg_stone src;

/*
 pprabhu:
 */

static int new_node_count = 1;

static void
join_handler(EVdfg dfg, char *identifier, void* available_sources, void *available_sinks)
{
	static char *canon_name;
	
	static int joined_node_count;
	static int stone_index = 1;
	
	EVdfg_stone sink = NULL;
	EVdfg_stone middle_stone = NULL;
	static EVdfg_stone last_stone = NULL;
	
	if (strcmp(identifier, "origin") == 0) {
		canon_name = strdup("origin");
		EVdfg_assign_canonical_name(dfg, identifier, canon_name);
		EVdfg_assign_node(src, "origin");
		last_stone = EVdfg_create_stone(dfg, NULL);
		EVdfg_assign_node(last_stone, "origin");
		EVdfg_link_port(src, 0, last_stone);
		++joined_node_count;
	}
	else {
		if (joined_node_count == 1) {
			canon_name = strdup("terminal");
			EVdfg_assign_canonical_name(dfg, identifier, canon_name);
			sink = EVdfg_create_sink_stone(dfg, "simple_handler");
			EVdfg_link_port(last_stone, 0, sink);
			EVdfg_assign_node(sink, "terminal");
			++joined_node_count;
			canon_name = malloc(20 * sizeof(char));
			EVdfg_realize(dfg);
		}
		else {
			if (joined_node_count == 2) {
				delayed_fork_children(cm, &reconfig_list[0], str_contact, 5);
				++joined_node_count;
			}
			else {
				sprintf(canon_name, "client%d", joined_node_count);
				EVdfg_assign_canonical_name(dfg, identifier, canon_name);
				middle_stone = EVdfg_create_stone(dfg, NULL);
				EVdfg_assign_node(middle_stone, canon_name);
				
				//      EVdfg_reconfig_link_port_to_stone(dfg, stone_index, 0, middle_stone, NULL);
				//      EVdfg_reconfig_link_port_from_stone(dfg, middle_stone, 0, 2, NULL);
				
				EVdfg_reconfig_insert(dfg, stone_index, middle_stone, 2, NULL);
				if (joined_node_count == 3) {
					stone_index+= 3;
				}
				else {
					stone_index+= 1;
				}
				++joined_node_count;
				
				if (joined_node_count == 13) {
					EVdfg_realize(dfg);
				}
			}
		}
	}
	
}


extern int
be_test_master(int argc, char **argv)
{
	
	/*
	 pprabhu:
	 */
	
    char **new_nodes;
	
    char **nodes;
	//    CManager cm;
    attr_list contact_list;
	//    char *str_contact;
    EVsource source_handle;
    int i;
	
    if (argc == 1) {
		sscanf(argv[0], "%d", &node_count);
    }
	
    cm = CManager_create();
    CMlisten(cm);
    contact_list = CMget_contact_list(cm);
    str_contact = attr_list_to_string(contact_list);
	
	/*
	 pprabhu:
	 */
	
	printf("\nMaster's contact: %s\n", str_contact);
	
	/*
	 **  LOCAL DFG SUPPORT   Sources and sinks that might or might not be utilized.
	 */
	
    source_handle = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("master_source", source_handle);
    EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
								(EVSimpleHandlerFunc) simple_handler);
	
	/*
	 **  DFG CREATION
	 */
    test_dfg = EVdfg_create(cm);
    EVdfg_node_join_handler(test_dfg, join_handler);
    src = EVdfg_create_source_stone(test_dfg, "master_source");
	
	
	/* pprabhu: creating list of reconfiguration node names */
    reconfig_list = malloc(sizeof(reconfig_list[0]) * (reconfig_node_count + 2));
    for (i=0; i < (reconfig_node_count + 1); i++) {
        reconfig_list[i] = strdup("client");
    }
    reconfig_list[reconfig_node_count + 1] = NULL;
	
	/* We're node 0 in the DFG */
    EVdfg_join_dfg(test_dfg, "origin", str_contact);
	
	/* Fork the others */
    nodes = malloc(sizeof(nodes[0])*(node_count+1));
    for (i=1; i < node_count; i++) {
		nodes[i] = strdup("client");
    }
    nodes[node_count] = NULL;
    test_fork_children(&nodes[0], str_contact);
	
    if (EVdfg_ready_wait(test_dfg) != 1) {
		/* dfg initialization failed! */
		exit(1);
    }
	
    
    if (EVdfg_active_sink_count(test_dfg) == 0) {
		EVdfg_ready_for_shutdown(test_dfg);
    }
	
    if (EVdfg_source_active(source_handle)) {
		simple_rec rec;
        for (i = 0; i < 300; ++i) {
			CMsleep(cm, 1);
			generate_simple_record(&rec);
			/* submit would be quietly ignored if source is not active */
			EVsubmit(source_handle, &rec, NULL);
        }
    }
	
    status = EVdfg_wait_for_shutdown(test_dfg);
	
    wait_for_children(nodes);
	
    CManager_close(cm);
    return status;
}


extern int
be_test_child(int argc, char **argv)
{
    CManager cm;
    EVsource src;
	
    cm = CManager_create();
    if (argc != 3) {
		printf("Child usage:  evtest  <nodename> <mastercontact>\n");
		exit(1);
    }
	
	printf("\nName = %s\n", argv[1]);
	
    test_dfg = EVdfg_create(cm);
	
    src = EVcreate_submit_handle(cm, -1, simple_format_list);
    EVdfg_register_source("master_source", src);
    EVdfg_register_sink_handler(cm, "simple_handler", simple_format_list,
								(EVSimpleHandlerFunc) simple_handler);
    EVdfg_join_dfg(test_dfg, argv[1], argv[2]);
    EVdfg_ready_wait(test_dfg);
	
    if (EVdfg_active_sink_count(test_dfg) == 0) {
		EVdfg_ready_for_shutdown(test_dfg);
    }
	
    if (EVdfg_source_active(src)) {
		simple_rec rec;
		generate_simple_record(&rec);
		/* submit would be quietly ignored if source is not active */
		EVsubmit(src, &rec, NULL);
    }
    return EVdfg_wait_for_shutdown(test_dfg);
}
