#include "config.h"
#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>

#include "cod.h"
#include "evpath.h"
#include "cm_internal.h"
#include "Weir/weir.h"
#include "revpath.h"
#include "Weir/weir_internal.h"
#include "revp_internal.h"
#undef NDEBUG
#include <assert.h>

#define weir_verbose_init(client)   \
	    if (getenv("WEIR_VERBOSE")) { \
		client->verbose = 1;\
	    }

#define weir_verbose(client, ...)                             \
            if(client->verbose) { \
		fprintf(stdout, "Client_ID %d: ", client->my_node_id); \
		fprintf(stdout, __VA_ARGS__);			   \
            }

#define ASSIGN_IMPL_SPECIFIC_FUNCTIONS(structure, type) \
    structure->impl_functions[type].create_graph = create_##type; \
    structure->impl_functions[type].get_port = get_port_##type; \

#define WEIR_FORWARD_DECLARE(a)  \
    static void create_##a(weir_group_info_ptr, weir_graph_node_ptr *, weir_master); \
    static int  get_port_##a(weir_client, attr_list); \

char *str_store_state[] = {"weir_Joining", "weir_Deploying", "weir_Deployed", "weir_Shutdown"};
static char *master_msg_str[] = {"weir_node_join", "weir_deploy_ack", "weir_shutdown", NULL};

static void handle_node_join(weir_master master, weir_master_msg_ptr msg);
static void handle_deploy_ack(weir_master master, weir_master_msg_ptr ptr);
static void handle_shutdown_msg(weir_master master, weir_master_msg_ptr msg);

static void
queue_master_msg(weir_master master, void*vmsg, weir_master_msg_type msg_type, CMConnection conn, int copy);
static void free_master_msg(weir_master_msg *msg);

static FMStructDescRec weir_deploy_ack_format_list[];
static FMStructDescRec weir_node_join_format_list[];
static FMStructDescRec weir_deploy_format_list[];
static FMStructDescRec weir_ready_format_list[];
static FMStructDescRec weir_shutdown_ack_format_list[];
static FMStructDescRec weir_shutdown_format_list[];
static weir_client global_client;
//static weir_master * gloabl_master_ptr;


WEIR_FORWARD_DECLARE(weir_tree_graph)
WEIR_FORWARD_DECLARE(weir_ring_graph)


/* msg action model
 *
 For each state/ for each master msg one of these possibilities:
	H - handle - dequeue and call handler (may change state, start over )
	U - unexpected - immediate error and discard (continue to next )
	I - ignore - discard (continue to next )
	L - leave_queued - (continue to next )

*/
static
char action_model[weir_Last_State][weir_last_msg] = {
/* join		deploy_ack	shutdown	*/
  {'H', 	'U',		'U'		},/* state Joining */
  {'U',		'H',		'U'		},/* state Deploying */
  {'U',		'U',		'H'		},/* state Deployed */
  {'U',		'U',		'U'		},/* state Shutdown */
};


static atom_t LAST_NODE_ID; 
static atom_t MY_NODE_ID;
static atom_t WEIR_TIMESTEP;

typedef void (*master_msg_handler_func) (weir_master master, weir_master_msg_ptr msg);
static master_msg_handler_func master_msg_handler[weir_last_msg] = {handle_node_join, handle_deploy_ack, handle_shutdown_msg};
static void store_master_msg_handler(CManager cm, CMConnection conn, void *vmsg, 
				   void *client_data, attr_list attrs);

/*static void
cod_weir_submit_local(void * data)
{
    weir_client client = global_client;
    CMtrace_out(client->cm, weirVerbose, "Weir cod submit local about to EVsubmit!\n");
    //EVsubmit(client->injector_source, data, NULL);
    CMtrace_out(client->cm, weirVerbose, "Finished submit, exiting weir_submit_local\n");
    
}
*/

static int 
cod_weir_get_port(attr_list attrs)
{
    weir_client client = global_client;
    //attr_list copy_of_attrs = attr_copy_list(attrs);
    //CMtrace_out(client->cm, weirVerbose, "Weir cod submit activated!\n");
    return client->impl_functions[client->type].get_port(client, attrs);
}


// For now, it will be understood that EVsubmit to 0 submits to your handler
// {"weir_submit_local", (void *) (long) cod_weir_submit_local},
// void weir_submit_local(void * data);\n

static cod_extern_entry weir_externs[] = { 
    {"weir_get_port", (void *) (long) cod_weir_get_port},
    {NULL, NULL}
};

static char weir_extern_string[] = "\
                                int weir_get_port(attr_list list);\0\0";

static void
handle_queued_messages(CManager cm, void* vmaster)
{
    /* SHOULD */
    /*  1 -  handle node join messages*/
    /*  2 - Increment counter for how many have joined */
    /* FOR THE MOMENT */
    /* just do everything in order */
    /* beware the the list might change while we're running a handler */
    weir_master master = (weir_master) vmaster;
    weir_master_msg_ptr next;
    weir_master_msg_ptr *last_ptr;

    if (master->queued_messages == NULL) return;
    assert(CManager_locked(cm));
    next = master->queued_messages;
    last_ptr = &master->queued_messages;
    while(next != NULL) {
	CMtrace_out(cm, weirVerbose, "weir handle_queued_messages -  master Store state is %s\n", str_store_state[master->state]);
	switch (action_model[master->state][next->msg_type]) {
	case 'H':
	    CMtrace_out(cm, weirVerbose, "Master Message is type %s, calling handler\n", master_msg_str[next->msg_type]);
	    *last_ptr = next->next;  /* remove msg from queue */
	    (*master_msg_handler[next->msg_type])(master, next);
	    free_master_msg(next);
	    next = master->queued_messages;   /* start from scratch in case state changed */
	    break;
	case 'U':
	    printf("Master Message is type %s, UNEXPECTED!  Discarding...\n", master_msg_str[next->msg_type]);
	    *last_ptr = next->next;  /* remove msg from queue */
	    free_master_msg(next);
	    next = *last_ptr;
	    break;
	default:
	    printf("Unexpected action type '%c', discarding\n", action_model[master->state][next->msg_type]);
	    /* falling through */
	}
	CMtrace_out(cm, weirVerbose, "weir handle queued end loop -  master Store state is now %s\n", str_store_state[master->state]);
    }	    
    CMtrace_out(cm, weirVerbose, "weir handle queued exiting -  master Store state is now %s\n", str_store_state[master->state]);
}

static void
handle_queued_messages_lock(CManager cm, void* vmaster)
{
    CManager_lock(cm);
    handle_queued_messages(cm, vmaster);
    CManager_unlock(cm);
}

static void check_all_nodes_participated(weir_master master);

static void
store_ready_handler(CManager cm, CMConnection conn, void *vmsg, 
		  void *client_data, attr_list attrs)
{
    weir_client client = client_data;
    (void) conn;
    (void) attrs;
    CMtrace_out(cm, weirVerbose, "Client Store %p Node id %d is ready, signalling %d\n", client, 
                                                        client->my_node_id, client->ready_condition);
    
    CMCondition_signal(cm, client->ready_condition);
}

static void
handle_shutdown_msg(weir_master master, weir_master_msg_ptr msg)
{
    CMtrace_out(master->cm, weirVerbose, "Master received shutdown from: %d\n", msg->u.shutdown_msg.node_id);
    master->nodes_received++; //This assumes one group, it will need to be more sophisticated for multiple groups...some early logic below...

    if(master->nodes_received == master->nodes_expected)
    {
	CMtrace_out(master->cm, weirVerbose, "All nodes indicated shutdown, sending reply messages!\n");
	/* Need this code for later with multiple groups probably...
	int number_of_total_groups = 0;
    	weir_group_info_ptr curr = master->groups_info;
    	while(curr != NULL)
    	{
    	    number_of_total_groups++;
    	    curr = curr->next;
    	}
	
	int k;
	for(k = 0; k < number_of_total_groups; k++)
	{
	    weir_group_info_ptr curr = master->groups_info;
	    int j;
	    for(j = 0; j < k; j++)
	    {
		curr = curr->next;
            	if(curr == NULL)
            	{
            	    fprintf(stderr, "Error: pointer is null when it shouldn't be in perform_deployment!\n");
            	    assert(0);
            	}
	    }
	*/
        int master_index = -1;
        if(master->client)
        {
            int i = 0;
            for(; i < master->nodes_expected; i++)
            {
                if(!master->nodes[i].str_contact_list)
                    break;
            }
            master_index = i;
        }

	int i;
        for(i = 0; i < master->nodes_expected; i++)
        {
            if(i == master_index)
            {
                INT_CMCondition_signal(master->client->cm, master->client->shutdown_condition);
                continue;
            }
	    weir_shutdown_ack_msg reply_msg;
	    reply_msg.shutdown_commence = master->nodes_received;
	    CMConnection conn = master->nodes[i].conn;
	    CMFormat shutdown_ack_format = INT_CMlookup_format(master->cm, weir_shutdown_ack_format_list);
	    INT_CMwrite(conn, shutdown_ack_format, &reply_msg);

        }
	master->state = weir_Shutdown;
    }
}


static void
handle_node_join(weir_master master, weir_master_msg_ptr msg)
{
    char *contact_string = msg->u.node_join.contact_string;

    CMConnection conn = msg->conn;
    int i;

    assert(CManager_locked(master->cm));
    int index = master->nodes_received++;

    if (conn != NULL)
    {
	INT_CMConnection_add_reference(conn);
	master->nodes[index].conn = conn;
    }

    if(contact_string)
    {
        master->nodes[index].str_contact_list = strdup(contact_string);
        master->nodes[index].contact_list = attr_list_from_string(master->nodes[index].str_contact_list);
    }
    else
    {
        master->nodes[index].str_contact_list = contact_string;
        master->nodes[index].contact_list = NULL;
    }


    master->nodes[index].number_of_groups = msg->u.node_join.number_groups;
    master->nodes[index].groups = malloc(sizeof(int) * master->nodes[index].number_of_groups);
    for(i = 0; i < master->nodes[index].number_of_groups; ++i)
    {
	int group_id = msg->u.node_join.groups[i];
	master->nodes[index].groups[i] = group_id;
	
	//Search for existing group
	weir_group_info_ptr curr = master->groups_info;
	weir_group_info_ptr prev = NULL;
	while(curr != NULL)
	{
	    if(group_id == curr->group_id)
	    {
                (curr->number_of_participants)++;
	        curr->str_contact_names = realloc(curr->str_contact_names, sizeof(char *) * curr->number_of_participants);
	        curr->local_stone_ids = realloc(curr->local_stone_ids, sizeof(int) * curr->number_of_participants);
                if(contact_string)
	            curr->str_contact_names[curr->number_of_participants - 1] = strdup(master->nodes[index].str_contact_list);
                else
                    curr->str_contact_names[curr->number_of_participants - 1] = strdup(master->my_contact_str);
	        curr->local_stone_ids[curr->number_of_participants - 1] = msg->u.node_join.local_id;
	        break;
	    }
	    prev = curr;
	    curr = curr->next;
	}

	//If we didn't find one create a new group
	if(!curr)
	{
	    weir_group_info_ptr new_group = calloc(1, sizeof(curr[0]));
	    new_group->group_id = group_id;
	    new_group->number_of_participants++;
	    new_group->str_contact_names = malloc(sizeof(char*));
            if(contact_string)
	        new_group->str_contact_names[0] = strdup(master->nodes[index].str_contact_list);
            else
                new_group->str_contact_names[0] = strdup(master->my_contact_str);
	    new_group->local_stone_ids = malloc(sizeof(int));
	    new_group->local_stone_ids[0] = msg->u.node_join.local_id;
            
            if(!prev)
                master->groups_info = new_group;
            else
	        prev->next = new_group;
	}
    }
		


    CMtrace_out(master->cm, weirVerbose, "Client has joined Store, contact %s\n", master->nodes[index].str_contact_list);
    check_all_nodes_participated(master);
}


static void
free_master(CManager cm, void *vmaster)
{
    weir_master master = (weir_master)vmaster;
    int i;
    for (i=0; i < master->nodes_expected; i++) {

	if (master->nodes[i].contact_list) 
	    free_attr_list(master->nodes[i].contact_list);

	if (master->nodes[i].str_contact_list) 
	    free(master->nodes[i].str_contact_list);
    
	if (master->nodes[i].number_of_groups > 0)
	    free(master->nodes[i].groups);
    }

    free(master->nodes);
    weir_group_info_ptr head = master->groups_info;
    while(head)
    {
	if(head->number_of_participants > 0) 
	{
	    for(i = 0; i < head->number_of_participants; ++i)
	    {
	        if(head->str_contact_names[i])
	    	    free(head->str_contact_names[i]);
	    }
	    free(head->str_contact_names);

	    free(head->local_stone_ids);
	}
	weir_group_info_ptr temp = head;
	head = head->next;
	free(temp);
    }
	    
    
    if (master->my_contact_str) free(master->my_contact_str);
    free(master);
}

static void
free_client(CManager cm, void *vclient)
{
    //TODO: This has some memory leaks but I'm in a hurry right now
    weir_client client = (weir_client)vclient;
    if (client->master_connection) 
	INT_CMConnection_close(client->master_connection);
    if (client->master_contact_str) free(client->master_contact_str);
    free(client);
}

extern weir_master
INT_weir_master_create(CManager cm, int number_expected_stores, weir_graph_type type)
{
    weir_master master = malloc(sizeof(struct _weir_master));
    attr_list contact_list;

    memset(master, 0, sizeof(struct _weir_master));
    master->cm = cm;
    master->state = weir_Joining;
    master->nodes_expected = number_expected_stores;
    master->type = type;
    master->impl_functions = malloc(sizeof(weir_implementation_functions) * weir_num_impl_types);


    ASSIGN_IMPL_SPECIFIC_FUNCTIONS(master, weir_tree_graph);
    ASSIGN_IMPL_SPECIFIC_FUNCTIONS(master, weir_ring_graph);

    master->nodes = malloc(sizeof(master->nodes[0]) * number_expected_stores);
    memset(master->nodes, 0, sizeof(master->nodes[0]) * number_expected_stores);

    //master->groups_info = malloc(sizeof(master->groups_info[0]));
    //memset(master->groups_info, 0, sizeof(master->groups_info[0]));
    
    CMtrace_out(cm, weirVerbose, "weir initialization -  master Store state set to %s\n", str_store_state[master->state]);
    contact_list = INT_CMget_contact_list(cm);
    if (contact_list == NULL) {
	INT_CMlisten(cm);
	contact_list = INT_CMget_contact_list(cm);
    }
    master->my_contact_str = attr_list_to_string(contact_list);
    CMtrace_out(cm, weirVerbose, "weir master contact string: %s\n", master->my_contact_str);
    free_attr_list(contact_list);

    /*
     * weir master-sent messages
     */
    INT_CMregister_format(cm, weir_ready_format_list);
    INT_CMregister_format(cm, weir_deploy_format_list);
    INT_CMregister_format(cm, weir_shutdown_ack_format_list);

    /*
     * weir master-handled messages
     */
    INT_CMregister_handler(INT_CMregister_format(cm, weir_node_join_format_list),
			   store_master_msg_handler, (void*)(((uintptr_t)master)|weir_node_join));
    INT_CMregister_handler(INT_CMregister_format(cm, weir_deploy_ack_format_list),
			   store_master_msg_handler, (void*)(((uintptr_t)master)|weir_deploy_ack));
    INT_CMregister_handler(INT_CMregister_format(cm, weir_shutdown_format_list),
			   store_master_msg_handler, (void*)(((uintptr_t)master)|weir_shutdown));

    INT_CMadd_poll(cm, handle_queued_messages_lock, master);
    INT_CMadd_shutdown_task(cm, free_master, master, FREE_TASK);
    return master;
}

extern char *INT_weir_master_get_contact_list(weir_master master)
{
    attr_list listen_list, contact_list = NULL;
    atom_t CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
    atom_t CM_ENET_CONN_TIMEOUT = attr_atom_from_string("CM_ENET_CONN_TIMEOUT");
    CManager cm = master->cm;
    char *tmp;

    /* use enet transport if available */
    listen_list = create_attr_list();
    add_string_attr(listen_list, CM_TRANSPORT, strdup("enet"));
    contact_list = INT_CMget_specific_contact_list(cm, listen_list);
    /* Wait 60 seconds for timeout */
    add_int_attr(contact_list, CM_ENET_CONN_TIMEOUT, 3600000);

    free_attr_list(listen_list);
    if (contact_list == NULL) {
	contact_list = INT_CMget_contact_list(cm);
	if (contact_list == NULL) {
	    CMlisten(cm);
	    contact_list = INT_CMget_contact_list(cm);
	}
    }
    tmp = attr_list_to_string(contact_list);
    free_attr_list(contact_list);
    return tmp;
}


extern int 
INT_weir_client_ready_wait(weir_client client)
{
    CMtrace_out(client->cm, weirVerbose, "Client %p wait for ready\n", client);
    INT_CMCondition_wait(client->cm, client->ready_condition);
    client->ready_condition = -1;
    CMtrace_out(client->cm, weirVerbose, "Client %p ready wait released\n", client);
    return 1;
}

/*Make sure you unlock around this when you call it from the master*/
static void
create_client_bridges(weir_client client, weir_deploy_ptr msg)
{
    int i;

    client->type = msg->graph_type;
    client->number_of_producer_bridges = msg->number_of_producer_connections;
    client->producer_bridges = malloc(sizeof(EVstone) * client->number_of_producer_bridges);

    client->number_of_consumer_bridges = msg->number_of_consumer_connections;
    client->consumer_bridges = malloc(sizeof(EVstone) * client->number_of_consumer_bridges);

    client->number_of_members_in_group = msg->number_in_group;
    client->my_node_id = msg->node_group_id;

    for(i = 0; i < client->number_of_consumer_bridges; i++)
    {
        int local_stone_id;
        char connection_str[1024];
        
        if(sscanf(msg->consumer_connections[i], "%d:%s", &local_stone_id, connection_str) != 2)
        {
            fprintf(stderr, "Error: connection string passed to client invalid format!!\n");
            fprintf(stderr, "Was Passed: %s\n", msg->consumer_connections[i]);
            assert(NULL);
        }

        attr_list contact_list;
        contact_list = attr_list_from_string(connection_str);
        client->consumer_bridges[i] = EValloc_stone(client->cm);
        EVassoc_bridge_action(client->cm, client->consumer_bridges[i], contact_list, local_stone_id);

        EVaction_add_split_target(client->cm, client->consumer_split, client->consumer_split_action, client->consumer_bridges[i]);
    }


    for(i = 0; i < client->number_of_producer_bridges; i++)
    {
        int local_stone_id;
        char connection_str[1024];
        
        if(sscanf(msg->producer_connections[i], "%d:%s", &local_stone_id, connection_str) != 2)
        {
            fprintf(stderr, "Error: connection string passed to client invalid format!!\n");
            fprintf(stderr, "Was Passed: %s\n", msg->producer_connections[i]);
            assert(NULL);
        }

        attr_list contact_list;
        contact_list = attr_list_from_string(connection_str);
        client->producer_bridges[i] = EValloc_stone(client->cm);
        EVassoc_bridge_action(client->cm, client->producer_bridges[i], contact_list, local_stone_id);

        EVaction_add_split_target(client->cm, client->producer_split, client->producer_split_action, client->producer_bridges[i]);
    }

    attr_list stone_attrs = INT_EVextract_attr_list(client->cm, client->store_stone);
    add_int_attr(stone_attrs, MY_NODE_ID, client->my_node_id);

}

static void
store_deploy_handler(CManager cm, CMConnection conn, void *vmsg, 
		  void *client_data, attr_list attrs)
{
    weir_client client = (weir_client) client_data;
    (void) conn;
    (void) attrs;
    weir_deploy_ptr msg = vmsg;
    
    create_client_bridges(client, msg);
    CMtrace_out(cm, weirVerbose, "Client %d getting Deploy message\n", client->my_node_id);


    weir_deploy_ack_msg new_msg;
    new_msg.node_id = client->my_contact_str;
    CMFormat deploy_ack_format = INT_CMlookup_format(client->cm, weir_deploy_ack_format_list);
    INT_CMwrite(client->master_connection, deploy_ack_format, &new_msg);

}

static void
shutdown_ack_handler(CManager cm, CMConnection conn, void * vmsg, void * client_data, attr_list attrs)
{
    weir_client client = (weir_client) client_data;
    CMCondition_signal(cm, client->shutdown_condition);
    CMtrace_out(cm, weirVerbose, "Client %d signaled shutdown message\n", client->my_node_id);
}
    


extern weir_client
store_assoc_client(CManager cm,  char *master_contact, weir_master master, char* storage_cod, FMStructDescList * format_list, 
                    EVclientStorageHandlerFunc callback_func, void * client_data, int * the_groups, int number_of_groups)
{

    attr_list master_attrs = NULL;
    CMFormat register_msg = NULL;
    CMConnection conn = NULL;
    weir_client client;

    LAST_NODE_ID  = attr_atom_from_string("last_node_id");
    MY_NODE_ID    = attr_atom_from_string("my_node_id");
    WEIR_TIMESTEP = attr_atom_from_string("weir_timestep");


    CMtrace_out(cm, weirVerbose, "Entered assoc_client function \n");
    //TODO:Reedit this code
    register_msg = INT_CMlookup_format(cm, weir_ready_format_list);
    if ((master && master->client) || (!master && register_msg)) {
	fprintf(stderr, "Rejecting attempt to associate a DFG client with another DFG or with the same DFG multiple tiems.\n");
	fprintf(stderr, "Only one call to weir_client_assoc() or weir_client_assoc_local() per CManager allowed.\n");
	return NULL;
    }

    client = malloc(sizeof(*client));
    memset(client, 0, sizeof(*client));
    weir_verbose_init(client);
    client->cm = cm;
    global_client = client;
    /* TODO: I need to refactor this part into a dedicated weir object that both the client and the master initialize...  For now
    ** we just need to make sure that the master and the client match each other in the create functions when it comes to the macro
    ** below */
    client->impl_functions = malloc(sizeof(weir_implementation_functions) * weir_num_impl_types);
    ASSIGN_IMPL_SPECIFIC_FUNCTIONS(client, weir_tree_graph);
    ASSIGN_IMPL_SPECIFIC_FUNCTIONS(client, weir_ring_graph);

    if(master_contact)
    {
        master_attrs = attr_list_from_string(master_contact);
        client->master_contact_str = strdup(master_contact);
    }
    else
    {
        client->master_contact_str = NULL;
        master->client = client;
    }


    attr_list contact_list = INT_CMget_contact_list(cm);
    if (contact_list == NULL) {
	INT_CMlisten(cm);
	contact_list = INT_CMget_contact_list(cm);
    }
    client->my_contact_str = attr_list_to_string(contact_list);
    free_attr_list(contact_list);
    

    client->ready_condition = INT_CMCondition_get(cm, NULL);
    client->shutdown_condition = INT_CMCondition_get(cm, NULL);
    CMtrace_out(cm, weirVerbose, "Got up to the ready condition initialization figured out!\n");

    //Set up handlers and internal formats
    register_msg = INT_CMregister_format(cm, weir_node_join_format_list);
    INT_CMregister_format(cm, weir_deploy_ack_format_list);
    INT_CMregister_format(cm, weir_shutdown_format_list);

    INT_CMregister_handler(INT_CMregister_format(cm, weir_ready_format_list),
			       store_ready_handler, client);

    INT_CMregister_handler(INT_CMregister_format(cm, weir_deploy_format_list),
			       store_deploy_handler, client);

    INT_CMregister_handler(INT_CMregister_format(cm, weir_shutdown_ack_format_list),
			       shutdown_ack_handler, client);

    CMtrace_out(cm, weirVerbose, "Registered handlers and formats correctly\n");


    if(!master)
    {
        conn = INT_CMget_conn(cm, master_attrs);
        if (conn == NULL) {
            fprintf(stderr, "failed to contact Master at %s\n", attr_list_to_string(master_attrs));
            fprintf(stderr, "Join Store failed\n");
            return NULL;
        }
    }
    client->master_connection = conn;
    client->master = master;

    //Set up multiqueue stone, terminal stone, injector stone combo
    client->store_stone = INT_EValloc_stone(client->cm);
    client->callback_stone = INT_EValloc_stone(client->cm);
    client->producer_split = INT_EValloc_stone(client->cm);
    client->consumer_split = INT_EValloc_stone(client->cm);

    char * temp_action_str = INT_create_multityped_action_spec(format_list, storage_cod);
    INT_EVassoc_multi_action(client->cm, client->store_stone, temp_action_str, NULL);
    client->consumer_split_action = INT_EVassoc_split_action(client->cm, client->consumer_split, NULL);
    client->producer_split_action = INT_EVassoc_split_action(client->cm, client->producer_split, NULL);
    INT_EVassoc_terminal_action(client->cm, client->callback_stone, format_list[0], callback_func, client_data);

    /*TODO:Change the way the below is done so that we can have the power of deciding what to share without forcing the user to know 
    **     how its shared
    */
    INT_EVstone_set_output(client->cm, client->store_stone, 0, client->callback_stone);
    INT_EVstone_set_output(client->cm, client->store_stone, 1, client->consumer_split);
    INT_EVstone_set_output(client->cm, client->store_stone, 2, client->producer_split);

    client->injector_source = INT_EVcreate_submit_handle(client->cm, client->store_stone, format_list[0]);
    client->to_producer_source = INT_EVcreate_submit_handle(client->cm, client->producer_split, format_list[0]);
    client->to_consumer_source = INT_EVcreate_submit_handle(client->cm, client->consumer_split, format_list[0]);

    CMtrace_out(cm, weirVerbose, "Set up the initial stones in the client correctly\n");

    //Store the first in the format list for later use
    client->store_format = format_list[0];

    //Send multiqueue stone and group information to master
    if(!conn)
    {
        CMtrace_out(cm, weirVerbose, "This is a local client, doing local magic things.\n");
        weir_node_join_msg msg;
        msg.contact_string = NULL;
        msg.local_id = client->store_stone;
        msg.number_groups = number_of_groups;
        msg.groups = the_groups;
        //Trying to insert myself into the critical path here...the handler is normally called from the 
        //network handler thread, so I'm worried about what will happen if it gets called and the CM is locked.
	//CManager_unlock(client->cm);
        store_master_msg_handler(client->cm, NULL, &msg, (void*)(((uintptr_t)master)|weir_node_join), NULL);
        //CManager_lock(client->cm);
    }
    else
    {
        weir_node_join_msg msg;
        msg.contact_string = client->my_contact_str;
        msg.local_id = client->store_stone;
        msg.number_groups = number_of_groups;
        msg.groups = the_groups;

        CMtrace_out(cm, weirVerbose, "About to write to the master the information for my node!\n");
        INT_CMwrite(conn, register_msg, &msg);
    }


    CMtrace_out(cm, weirVerbose, "Store %p \n", client);
    if (master_attrs) free_attr_list(master_attrs);
    INT_CMadd_shutdown_task(cm, free_client, client, FREE_TASK);

    INT_EVadd_standard_routines(cm, weir_extern_string, weir_externs);

    return client;
}

extern int
INT_weir_get_number_msgs_in_subtree(weir_client client)
{
    return client->number_of_msgs_in_subgraph;
}

extern weir_client
INT_weir_client_assoc_local(CManager cm, weir_master master, char* storage_cod, FMStructDescList * format_list,
			     EVclientStorageHandlerFunc handler_func, void * client_data,  int * the_groups, int number_of_groups)
{
    return store_assoc_client(cm, NULL, master, storage_cod, format_list, handler_func, client_data, the_groups, number_of_groups);
}


extern weir_client
INT_weir_client_assoc(CManager cm, char *master_contact_str, char* storage_cod, FMStructDescList * format_list, 
                            EVclientStorageHandlerFunc handler_func, void * client_data, int * the_groups, int number_of_groups)
{
    return store_assoc_client(cm, master_contact_str, NULL, storage_cod, format_list, handler_func, client_data, the_groups, number_of_groups);
}

static void
free_master_msg(weir_master_msg *msg)
{
    switch(msg->msg_type) {
    case weir_node_join: {
	weir_node_join_ptr in = &msg->u.node_join;
	free(in->contact_string);
	break;
    }
    case weir_shutdown:
    case weir_deploy_ack:
    default:
	break;
    }
    free(msg);
}

static void
queue_master_msg(weir_master master, void*vmsg, weir_master_msg_type msg_type, CMConnection conn, int copy)
{
    weir_master_msg_ptr msg = malloc(sizeof(weir_master_msg));
    msg->msg_type = msg_type;
    msg->conn = conn;
    switch(msg_type) {
    case weir_node_join: {
	weir_node_join_ptr in = (weir_node_join_ptr)vmsg;
	if (!copy) {
	    msg->u.node_join = *in;
	} else {
	    int i;
            if(in->contact_string)
	        msg->u.node_join.contact_string = strdup(in->contact_string);
            else
                msg->u.node_join.contact_string = NULL;
	    msg->u.node_join.local_id = in->local_id;
	    msg->u.node_join.number_groups = in->number_groups;
	    msg->u.node_join.groups = (int *) malloc(sizeof(int) * msg->u.node_join.number_groups);
	    for(i = 0; i < msg->u.node_join.number_groups; i++)
	    {
		msg->u.node_join.groups[i] = in->groups[i];
	    }
	}
	break;
    }
    case weir_deploy_ack: {
	weir_deploy_ack_ptr in = (weir_deploy_ack_ptr)vmsg;
	msg->u.deploy_ack = *in;
	break;
    }
    case weir_shutdown: {
	weir_shutdown_msg_ptr in = (weir_shutdown_msg_ptr)vmsg;
	msg->u.shutdown_msg = *in;
	break;
    }
    default:
	printf("Message type bad, value is %d  %d\n", msg_type, msg->msg_type);
	assert(FALSE);
    }
    msg->next = NULL;
    if (master->queued_messages == NULL) {
	master->queued_messages = msg;
    } else {
	weir_master_msg_ptr last = master->queued_messages;
	while (last->next != NULL) last = last->next;
	last->next = msg;
    }
    if (master->cm->control_list->server_thread != 0) {
	CMwake_server_thread(master->cm);
    } else {
	handle_queued_messages(master->cm, master);
    }
}

static void
store_master_msg_handler(CManager cm, CMConnection conn, void *vmsg, 
		       void *client_data, attr_list attrs)
{
    weir_master master = (weir_master)((uintptr_t)client_data & (~0x7));
    weir_master_msg_type msg_type = ((uintptr_t)client_data & 0x7);
    queue_master_msg(master, vmsg, msg_type, conn, /*copy*/1);
    /* we'll handle this in the poll handler */
}

static void
handle_deploy_ack(weir_master master, weir_master_msg_ptr mmsg)
{
    weir_deploy_ack_ptr msg =  &mmsg->u.deploy_ack;
    CManager cm = master->cm;

    master->nodes_received--;

    CMtrace_out(cm, weirVerbose, "Client %s reports deployed, count %d\n", msg->node_id, 
                                                                                (master->nodes_expected - master->nodes_received));
    if(master->nodes_received == 0)
    {
        
        CMtrace_out(cm, weirVerbose, "All acks returned, sending the ready message!\n");
        int master_index = -1;
        if(master->client)
        {
            int i = 0;
            for(; i < master->nodes_expected; i++)
            {
                if(!master->nodes[i].str_contact_list)
                    break;
            }
            master_index = i;
        }

	int i;
        for(i = 0; i < master->nodes_expected; i++)
        {
            if(i == master_index)
            {
                INT_CMCondition_signal(master->client->cm, master->client->ready_condition);
                continue;
            }
            weir_ready_msg temp;
            temp.graph_type = master->type;
            CMConnection conn = master->nodes[i].conn;
            CMFormat ready_msg_format = INT_CMlookup_format(master->cm, weir_ready_format_list);
            INT_CMwrite(conn, ready_msg_format, &temp);
        }
        master->state = weir_Deployed;
    }

    CMtrace_out(cm, weirVerbose, "weir exit deploy ack handler -  master state is %s\n", str_store_state[master->state]);
}
//Above are all the calls our different_implementations might make

#include "diff_implementations.h"

static void
inform_weir_node(weir_master master, weir_graph_node_ptr curr_node, int number_of_participants)
{
    if(!curr_node)
    {
        fprintf(stderr, "Error: logic is informing a null tree graph node and this is unexpected from the flow of logic!\n");
	return;
    }

    CMConnection conn;
    weir_deploy_msg msg;
    int i;
    CMFormat deploy_msg = INT_CMlookup_format(master->cm, weir_deploy_format_list);
    /* make valgrind happy with the memset */
    memset(&msg, 0, sizeof(msg));
    msg.number_in_group = number_of_participants;
    msg.graph_type = master->type;
    msg.node_group_id = curr_node->node_group_id;

    //Prep the message
    //0 incoming edges, must be the root node


    msg.number_of_consumer_connections = curr_node->number_of_consumers;
    msg.consumer_connections = malloc(sizeof(char * ) * msg.number_of_consumer_connections);
    for(i = 0; i < msg.number_of_consumer_connections; i++)
    {
        msg.consumer_connections[i] = strdup(curr_node->consumers[i]->str_contact);
    }
    if(msg.number_of_consumer_connections == 0)
        msg.consumer_connections = NULL;

    msg.number_of_producer_connections = curr_node->number_of_producers;
    msg.producer_connections = malloc(sizeof(char * ) * msg.number_of_producer_connections);
    for(i = 0; i < msg.number_of_producer_connections; i++)
    {
        msg.producer_connections[i] = strdup(curr_node->producers[i]->str_contact);
    }
    if(msg.number_of_producer_connections == 0)
        msg.producer_connections = NULL;
    

    char temp[100];
    char * colon = strpbrk(curr_node->str_contact, ":");
    if(!colon)
	fprintf(stderr, "Error: couldn't find colon when trying to inform the tree\n");
    
    
    if(!strcpy(temp, ++colon))
	fprintf(stderr, "Error: didn't copy anything when trying to inform tree, contact will fail!\n");


    if(strcmp(temp, master->my_contact_str) == 0)
    {
        CMtrace_out(master->cm, weirVerbose, "This is the master node, creating the client bridges manually!\n");
        //Need to unlock here because create_client bridges is called from inside a handler othewise, which has
        //everything unlocked
	CManager_unlock(master->client->cm);
        create_client_bridges(master->client, &msg);
        CManager_lock(master->client->cm);
        master->nodes_received--;
    }
    else
    {
        CMtrace_out(master->cm, weirVerbose, "Informing a non master node of the client bridges!\n");
        attr_list client_attrs = attr_list_from_string(temp);
        conn = INT_CMget_conn(master->cm, client_attrs);
        
        if (conn == NULL) {
            fprintf(stderr, "Failed to contact client at %s\n", attr_list_to_string(client_attrs));
            fprintf(stderr, "Inform tree failed!\n");
            return;
        }

        INT_CMwrite(conn, deploy_msg, &msg);
    }
    
    //Notify down the tree
    //This isn't going to work because our ring is circular
    /*for(i = 0; i < curr_node->number_of_consumers; i++)
    {
        inform_weir_graph(master, curr_node->consumers[i], number_of_participants);
    }
    */
}

static void
perform_deployment(weir_master master, int number_of_graphs)
{
    weir_graph_node_ptr * graphs = master->graphs;
    int i;
    for(i = 0; i < number_of_graphs; ++i)
    {
        weir_group_info_ptr curr = master->groups_info;
	int j;
        for(j = 0; j < i; j++)
        {
            curr = curr->next;
            if(curr == NULL)
            {
                fprintf(stderr, "Error: pointer is null when it shouldn't be in perform_deployment!\n");
                assert(0);
            }
        }
	int k;
	for(k = 0; k < curr->number_of_participants; ++k)
	{
	    inform_weir_node(master, &(graphs[i][k]), curr->number_of_participants);//Got myself in a pickel here trying to write multiple groups in
	}
        //master->impl_functions[master->type].inform_graph(master, graphs[i], curr->number_of_participants);
    }    
    CMtrace_out(master->cm, weirVerbose, "Finished performing the deployment!\n");
}

/*
static void
wait_for_deploy_acks(EVdfg dfg)
{
    if (dfg->deploy_ack_count != dfg->master->node_count) {
	if (dfg->deploy_ack_condition != -1)  {
	    CManager_unlock(dfg->master->cm);
	    CMCondition_wait(dfg->master->cm, dfg->deploy_ack_condition);
	    CManager_lock(dfg->master->cm);
	}
    }
}
*/



static void
create_weir_graphs(weir_graph_node_ptr * graphs, weir_master master)
{
    weir_group_info_ptr curr_info = master->groups_info;
    weir_graph_node_ptr * curr_graph = graphs;

    while(curr_info != NULL)
    {
        master->impl_functions[master->type].create_graph(curr_info, curr_graph, master);
        curr_info = curr_info->next;
        curr_graph = graphs + 1;
    }
    curr_info = master->groups_info;
    curr_graph = graphs;

    if(getenv("weirPrintGraph"))
    {
        int current_graph_temp_id = 0;
        while(curr_info != NULL)
        {
            char * filename = malloc(100 * sizeof(char));
            sprintf(filename, "weir_output_graph_%d.dot", current_graph_temp_id);
            FILE * output_file = fopen(filename, "w");
            fprintf(output_file, "digraph weir_graph {\n");
            int j;
            for(j = 0; j < curr_info->number_of_participants; j++)
            {
                int i;
                for(i = 0; i < (*curr_graph)[j].number_of_consumers; i++)
                    fprintf(output_file, "%d->%d\n", (*curr_graph)[j].node_group_id, (*curr_graph)[j].consumers[i]->node_group_id);
                for(i = 0; i < (*curr_graph)[j].number_of_producers; i++)
                    fprintf(output_file, "%d->%d\n", (*curr_graph)[j].node_group_id, (*curr_graph)[j].producers[i]->node_group_id);
            }
            curr_info = curr_info->next;
            curr_graph = graphs + 1;
            current_graph_temp_id++;
            fprintf(output_file, "}");
            fclose(output_file);
        }
    }
}
	    

static void
check_all_nodes_participated(weir_master master)
{
    if(master->nodes_expected != master->nodes_received)
	return;

    CMtrace_out(master->cm, weirVerbose, "All nodes received, moving to deploy logic!\n");
    int number_of_total_groups = 0;
    weir_group_info_ptr curr = master->groups_info;
    while(curr != NULL)
    {
	number_of_total_groups++;
	curr = curr->next;
    }

    master->graphs = malloc(sizeof(weir_graph_node_ptr) * number_of_total_groups);
    create_weir_graphs(master->graphs, master);
    //create_storage_trees(master->graphs, master);
    
    master->state = weir_Deploying;
    perform_deployment(master, number_of_total_groups);
}

extern int 
INT_weir_get_number_in_group(weir_client client)
{
    return client->number_of_members_in_group;
}

extern int 
INT_weir_get_client_id_in_group(weir_client client)
{
    return client->my_node_id;
}

extern void
INT_weir_submit(weir_client client, void * data, attr_list attrs)
{
    attr_list extra_attrs = attrs;
    if(!extra_attrs)
    {
        extra_attrs = create_attr_list();
    }
    add_int_attr(extra_attrs, LAST_NODE_ID, client->my_node_id);
    add_int_attr(extra_attrs, WEIR_TIMESTEP, 0);
    INT_EVsubmit(client->injector_source, data, extra_attrs);
}

extern void
INT_weir_set_ring_size(weir_master master, int inner_size)
{
    master->ring_inner_size = inner_size;
}


extern void
INT_weir_send_shutdown_and_wait(weir_client client)
{
    while(client->number_of_msgs_in_subgraph > 0)
    {
	INT_CMusleep(client->cm, 10000);
    }
    weir_shutdown_msg new_msg;
    new_msg.node_id = client->my_node_id;

    if(!client->master_contact_str)
    {
	store_master_msg_handler(client->cm, NULL, &new_msg, (void*)(((uintptr_t)client->master)|weir_shutdown), NULL);
    }
    else
    {
	CMFormat shutdown_format = INT_CMlookup_format(client->cm, weir_shutdown_format_list);
	CMtrace_out(client->cm, weirVerbose, "Client %d waiting for shutdown message\n", client->my_node_id);
	INT_CMwrite(client->master_connection, shutdown_format, &new_msg);
    }

	INT_CMCondition_wait(client->cm, client->shutdown_condition);
}


static FMField EVnode_join_msg_flds[] = {
    {"contact_string", "string", sizeof(char*), FMOffset(weir_node_join_ptr, contact_string)},
    {"local_id", "integer", sizeof(int), FMOffset(weir_node_join_ptr, local_id)},
    {"number_groups", "integer", sizeof(int), FMOffset(weir_node_join_ptr, number_groups)},
    {"groups", "integer[number_groups]", sizeof(int), FMOffset(weir_node_join_ptr, groups)},
    {NULL, NULL, 0, 0}
};

static FMStructDescRec weir_node_join_format_list[] = {
    {"EVdfg_node_join", EVnode_join_msg_flds, sizeof(weir_node_join_msg), NULL},
    {NULL, NULL, 0, NULL}
};

static FMField EVready_msg_flds[] = {
    {"graph_type", "integer", sizeof(int), FMOffset(weir_ready_ptr, graph_type)},
    {NULL, NULL, 0, 0}
};

static FMStructDescRec weir_ready_format_list[] = {
    {"weir_ready", EVready_msg_flds, sizeof(weir_ready_msg), NULL},
    {NULL, NULL, 0, NULL}
};

static FMField EVdeploy_ack_msg_flds[] = {
    {"node_id", "string", sizeof(char*), FMOffset(weir_deploy_ack_ptr, node_id)},
    {NULL, NULL, 0, 0}
};

static FMStructDescRec weir_deploy_ack_format_list[] = {
    {"EVdfg_deploy_ack", EVdeploy_ack_msg_flds, sizeof(weir_deploy_ack_msg), NULL},
    {NULL, NULL, 0, NULL}
};

static FMField weir_shutdown_msg_flds[] = {
    {"node_id", "integer", sizeof(int), FMOffset(weir_shutdown_msg_ptr, node_id)},
    {NULL, NULL, 0,  0}
};

static FMStructDescRec weir_shutdown_format_list[] = {
    {"weir_shutdown_format_list", weir_shutdown_msg_flds, sizeof(weir_shutdown_msg_flds), NULL},
    {NULL, NULL, 0, NULL}
};

static FMField weir_shutdown_ack_msg_flds[] = {
    {"shutdown_commence", "integer", sizeof(char*), FMOffset(weir_shutdown_ack_msg_ptr, shutdown_commence)},
    {NULL, NULL, 0,  0}
};

static FMStructDescRec weir_shutdown_ack_format_list[] = {
    {"weir_shutdown_ack_format_list", weir_shutdown_ack_msg_flds, sizeof(weir_shutdown_ack_msg_flds), NULL},
    {NULL, NULL, 0, NULL}
};

static FMField weir_deploy_msg_flds[] = {
    {"node_group_id", "integer", sizeof(int), FMOffset(weir_deploy_ptr, node_group_id)},
    {"number_in_group", "integer", sizeof(int), FMOffset(weir_deploy_ptr, number_in_group)},
    {"number_of_producer_connections", "integer", sizeof(int), FMOffset(weir_deploy_ptr, number_of_producer_connections)},
    {"number_of_consumer_connections", "integer", sizeof(int), FMOffset(weir_deploy_ptr, number_of_consumer_connections)},
    {"producer_connections", "string[number_of_producer_connections]", sizeof(char *), FMOffset(weir_deploy_ptr, producer_connections)},
    {"consumer_connections", "string[number_of_consumer_connections]", sizeof(char *), FMOffset(weir_deploy_ptr, consumer_connections)},

    {NULL, NULL, 0, 0}
};

static FMStructDescRec weir_deploy_format_list[] = {
    {"EVdfg_deploy", weir_deploy_msg_flds, sizeof(weir_deploy_msg), NULL},
    {NULL, NULL, 0, NULL}
};


