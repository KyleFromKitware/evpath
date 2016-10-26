
typedef struct _weir_group_info {
    int			            group_id;
    int			            number_of_participants;
    char ** 		            str_contact_names;
    int	 *  		            local_stone_ids;
    struct _weir_group_info *    next;
}   weir_group_info, * weir_group_info_ptr;

typedef struct _weir_graph_node{
    char * str_contact;
    int node_group_id;
    int number_of_consumers;
    int number_of_producers;
    struct _weir_graph_node ** producers;
    struct _weir_graph_node ** consumers;
}   weir_graph_node, * weir_graph_node_ptr;

    


/*
 * Store join is sent by clients to the master at weir_join()
 */
typedef struct _weir_join_msg {
    char*  contact_string;
    int	   local_id;
    int	   number_groups;
    int*   groups;
} weir_node_join_msg, *weir_node_join_ptr;

/*
 * EVready is sent by master to the clients when the weir should start
 */
typedef struct _weir_ready_msg {
    int graph_type;
} weir_ready_msg, *weir_ready_ptr;

/*
 * Deploy_ack is sent by clients to master after a deploy has been completed
 */
typedef struct _weir_deploy_ack_msg {
    char *node_id;
} weir_deploy_ack_msg, *weir_deploy_ack_ptr;

/*
 * Shutdown is sent from clients to master when they want to begin the shutdown process
 */
typedef struct _weir_shutdown_msg {
    int node_id;
} weir_shutdown_msg, * weir_shutdown_msg_ptr;

/*
 * Shutdown ack is sent from master to clients when it receives a shutdown from every client process
 */
typedef struct _weir_shutdown_ack_msg {
    int shutdown_commence;
} weir_shutdown_ack_msg, * weir_shutdown_ack_msg_ptr;


/*
 * Deploy is sent by master to clients in order to deploy many stones at once.
 */
typedef struct _weir_deploy_msg {
    int node_group_id;
    int graph_type;
    int number_in_group;
    int number_of_producer_connections;
    int number_of_consumer_connections;
    char ** producer_connections;
    char ** consumer_connections;
} weir_deploy_msg, *weir_deploy_ptr;

typedef enum {weir_node_join=0, weir_deploy_ack=1, weir_shutdown=2, weir_last_msg} weir_master_msg_type;

/*
 * data structure used to maintain master's incoming message queue
 */
typedef struct _weir_master_msg {
    weir_master_msg_type msg_type;
    CMConnection conn;
    union {
	weir_node_join_msg  node_join;
	weir_deploy_ack_msg deploy_ack;
	weir_shutdown_msg   shutdown_msg;
    } u;
    struct _weir_master_msg *next;
} weir_master_msg, *weir_master_msg_ptr;


typedef struct _weir_int_node_rec {
    attr_list	    		contact_list;
    char *	    		str_contact_list;
    CMConnection    		conn;
    int		    		number_of_groups;
    int  *	    		groups;
} *weir_int_node_list;

typedef enum {weir_Joining=0, weir_Deploying=1, weir_Deployed=2, weir_Shutdown=3, weir_Last_State} weir_state;
extern char *str_store_state[];

#define STATUS_FAILED -3
#define STATUS_UNDETERMINED -2
#define STATUS_NO_CONTRIBUTION -1
#define STATUS_SUCCESS 0
#define STATUS_FAILURE 1
#define STATUS_FORCE 0x10000

typedef struct _weir_implementation_functions {
    void (*create_graph)(weir_group_info_ptr, weir_graph_node_ptr *, weir_master);
    int  (*get_port)(weir_client, attr_list);
} weir_implementation_functions;
     

struct _weir_master {
    CManager                            cm;
    weir_master_msg_ptr                 queued_messages;
    weir_state	                        state;
    weir_graph_type                     type;
    weir_implementation_functions *     impl_functions;
    int			                nodes_expected;
    int			                nodes_received;
    weir_int_node_list                  nodes;
    weir_group_info_ptr                 groups_info;
    weir_graph_node_ptr*                graphs;
    weir_client	                        client;
    char *		                my_contact_str;
    int                                 ring_inner_size;
};

struct _weir_client {
    CManager                            cm;
    weir_master                         master;
    char *                              master_contact_str;
    char *                              my_contact_str;
    weir_implementation_functions *     impl_functions; //This should probably be in a weir object like dfg, but I got rid of it at the beginning
    weir_graph_type                     type;
    EVstone                             store_stone;
    EVstone *                           producer_bridges; //For going back up the graph, we are bidirectional this way
    EVstone *                           consumer_bridges;
    EVstone                             producer_split;
    EVstone                             consumer_split; 
    EVstone                             callback_stone;
    EVaction                            consumer_split_action;
    EVaction                            producer_split_action;
    EVclientStorageHandlerFunc          callback_func;
    //EVsource *                          store_sources;
    EVsource                            injector_source;
    EVsource                            to_producer_source;
    EVsource                            to_consumer_source;
    int                                 number_of_producer_bridges;
    int                                 number_of_consumer_bridges;
    int                                 number_of_members_in_group;
    int					number_of_msgs_in_subgraph;
    FMStructDescRec *                   store_format;
    int                                 ready_condition;
    int					shutdown_condition;
    CMConnection                        master_connection;
    int                                 my_node_id;
    int					verbose;
};


extern weir_master INT_weir_master_create(CManager cm, int number_expected_stores, weir_graph_type type);

extern weir_client INT_weir_client_assoc(CManager cm, char *master_contact_str, char* storage_cod, FMStructDescList *format_list,
                                                EVclientStorageHandlerFunc handler_func, void * client_data, int * the_groups, int number_of_groups);

extern weir_client INT_weir_client_assoc_local(CManager cm, weir_master master, char* storage_cod, FMStructDescList *format_list,
                                                EVclientStorageHandlerFunc handler_func, void * client_data, int * the_groups, int number_of_groups);

extern void INT_weir_set_ring_size(weir_master master, int ring_inner_size);

extern char* INT_weir_master_get_contact_list ( weir_master master );
extern int INT_weir_client_ready_wait ( weir_client client );

extern int INT_weir_get_number_in_group(weir_client client);
extern int INT_weir_get_client_id_in_group(weir_client client);
extern int INT_weir_get_number_msgs_in_subtree(weir_client client);

extern void INT_weir_submit(weir_client client, void * data, attr_list attrs);
extern void INT_weir_send_shutdown_and_wait(weir_client client);
