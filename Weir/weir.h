#ifndef __EV_STORE__H__
/*! \file */

#include "evpath.h"

#ifdef	__cplusplus
extern "C" {
#endif
/** @defgroup ev_store weir functions and types
 * @{
 */
/*
**  Basic approach:
**  Create a EVPath system on the local node complete with bridge stones, nothing is managed here
**  Provide an API that will create data pools in the streaming EVPath environment, no DFG required,
**  and all complexity handled underneath the covers by weir
**  Actual deployment:
**  - first version:
**    * Master and slave relationship, master is informed of the number of participants in the weir
**	transaction
**    * Slaves block until they get the okay from the master that everything is ready to go
**    * Master transfers all data that the slaves need to know at the weir realize phase
**    * This version is not dynamic, static only.
**    * Consistency details?
**  - next version
**    * Allow for dynamic joining of stones to a group through weir function calls
**  - final version
**    * Remove central master and slave bootstrap
**  
**
*/

/*!
 * weir_master is a handle to a Store master, the distinguished process who manages bootstrapping.
 *
 * weir_master is an opaque handle
 */
typedef struct _weir_master * weir_master;

/*!
 * EVclient is a handle to a client process which can host DFG components.
 *
 * EVclient is an opaque handle
 */
typedef struct _weir_client * weir_client;


typedef enum {weir_tree_graph=0, weir_ring_graph, weir_num_impl_types} weir_graph_type;

/*!
 * Create a store master
 *
 * This call is used in master process to create the necessary initial 
 * set up for the bootstrapping process
 *
 * \param cm The CManager with which to associate the master
 * \param int The number of expected nodes that will call in to the master
 * \return An weir_master handle, to be used in later calls.
 */
extern weir_master weir_master_create(CManager cm, int number_expected_nodes, weir_graph_type type);

/*!
 * Get the contact list from an weir_master handle
 *
 * This call is used to extract contact information from an
 * weir_master handle.  This call is made on the Master side of weir
 * and the contact information is then provided to the remote Clients
 * for use in EVclient_assoc() calls.  The result of this call is a
 * null-terminated string to be owned by the caller.  (I.E. you should
 * free the string memory when you're done with it.)
 *
 * \param master The weir_master handle for which to create contact information.
 * \return A null-terminated string representing contact information for this EVdfg
 */
extern char *weir_master_get_contact_list(weir_master master);

typedef int  (*EVclientStorageHandlerFunc) (CManager cm, void * vevent, void * client_data, attr_list attrs);

/*!
 *  Associate this process as a client to an weir_master
 *
 *  This call is used to join the set of communicating processes as a
 * client.  The master_contact string should be the same one that came from
 * weir_master_get_contact_list() on the master.  This call cannot be used by
 * the master process to participate in the DFG itself.  In that
 * circumstance, EVclient_assoc_local() should be used.
 *
 * The source_capabilities and sink_capabilities parameters identify the set
 * of sinks and sources that this client is capable of hosting.  Those
 * capabilities may or may not be utilized in the DFGs created by the
 * master.  All calls to register sinks and sources should be done *before*
 * the EVclient_assoc() call.
 *
 * \param cm The CManager with which to associate the DFG client
 * \param node_name The name with which the client can be identified.  This
 *  should be unique among the joining nodes in static joining mode
 *  (I.E. using weir_master_register_node_list().  In dynamic mode (I.E. where
 *  weir_master_node_join_handler() is used), then this name is presented to the
 *  registered join handler, but it need not be unique.  EVdfg copies this
 *  string, so it can be free'd after use.
 * \param master_contact The string contact information for the master
 *  process.  This is not stored by EVdfg.
 *  \param collect_stone 
 */
extern weir_client
weir_client_assoc(CManager cm, char *master_contact_str, char* storage_cod, FMStructDescList * format_list,
                                    EVclientStorageHandlerFunc handler_func, void * client_data, int * the_groups, int number_of_groups);

extern weir_client
weir_client_assoc_local(CManager cm, weir_master master, char* storage_cod, FMStructDescList * format_list,
                                    EVclientStorageHandlerFunc handler_func,void * client_data, int * the_groups, int number_of_groups);

/*
 * This function is used to get the number of other clients in a group so that we can construct appropriate
 * objects.
 *
*/
extern int
weir_get_number_in_group(weir_client client);

/*
 * This function is used to get our identification marker in the group so we don't overwrite each others
 * information.
*/
extern int
weir_get_client_id_in_group(weir_client client);

extern int
weir_get_number_msgs_in_subtree(weir_client client);
/*
 *This function is used to submit new information to your store group
 *
*/
extern void
weir_submit(weir_client client, void * data, attr_list attrs);


/*
 *This function is used to set inner and outer size of the ring implementation.
 *This was built for research purposes.
 *
*/

extern void
weir_set_ring_size(weir_master master, int inner_size);
/*
 * The prototype for an weir_master client-join handling function.
 *
 * In "dynamic join" mode, as opposed to static-client-list mode (See
 * weir_master_register_node_list()), the EVdfg master calls a registered join
 * handler each time a new client node joins the DFG.  This handler should
 * 1) potentially assign a canonical name to the client node (using
 * weir_master_assign_canonical_name()), and 2) when the expected nodes have
 * all joined the handler should create the virtual DFG and then call
 * EVdfg_realize() in order to instantiate it.
 * 
 * This call happens in the Master process only.
 * 
 * \param dfg The EVdfg handle with which this handler is associated
 * \param identifier This null-terminated string is the value specified in
 * the corresponding EVclient_assoc() call by a client.
 * \param available_sources This parameter is currently a placeholder for
 * information about what sources the client is capable of hosting.
 * \param available_sinks This parameter is currently a placeholder for
 * information about what sinks the client is capable of hosting.
 */
//typedef void (*EVmasterJoinHandlerFunc) (EVmaster master, char *identifier, void* available_sources, void *available_sinks);


extern void 
weir_send_shutdown_and_wait(weir_client client);


/*!
 * The prototype for an EVdfg client-fail handling function.
 *
 * If the an EVmasterFailHandlerFunc has been registered to the DFG, this
 * function will be called in the event that some node has failed.
 * Generally EVdfg becomes aware that a node has failed when some other
 * client tries to do a write to a bridge stone connected to that node and
 * that write fails.  Three things to be cognizant of:
 *  - This call happens in the Master process only.
 *  - If it's the Master that has failed, you're not going to get a call.
 *  - If multiple nodes notice the same failure, you might get multiple
 * calls to this function resulting from a single failure.
 * 
 * \param dfg The EVdfg handle with which this handler is associated
 * \param failed_client This null-terminated string is the value that was
 * specified in EVclient_assoc() in the failed client.
 * \param failed_stone This is local ID of the failed stone (perhaps not
 * useful to anyone - should change or eliminate this).
 */
//typedef void (*EVmasterFailHandlerFunc) (EVdfg dfg, char *failed_client, int failed_stone);


/*!
 * Register a node join handler function to an EVmaster.
 *
 * \param master The EVmaster handle with which to associate this handler
 * \param func The handler function to associate
 */
//extern void EVmaster_node_join_handler (EVmaster master, EVmasterJoinHandlerFunc func);

/*!
 * Register a node fail handler function to an EVmaster
 *
 * \param master The EVmaster handle with which to associate this handler
 * \param func The handler function to associate
 */
//extern void EVmaster_node_fail_handler (EVmaster master, EVmasterFailHandlerFunc func);


/*!
 * Wait for the store process to be ready to run
 *
 *  This call is performed by participating clients (including the master
 *  process operating as a client), in order to wait for the deployment of a
 *  DFG.  This call will only return after a DFG has been completely
 *  instantiated and is running.  In initial deployment, all participating
 *  clients will exit this call at roughly the same time.  In the case of
 *  client nodes that join after a DFG is already running, this call will
 *  return after the master has been notified of the join and the client has
 *  been incorporated into the DFG (I.E. stones potentially deployed.)
 *
 * \param client The EVclient handle upon which to wait.
 * \return 1 on success, 0 on failure
 */
extern int weir_client_ready_wait(weir_client client);

/*!
 *  Assign a unique, canonical name to a client of a particular given_name.
 *
 *  This call is performed by the master, typically in the
 *  EVmasterJoinHandlerFunc, in order to assign a unique name to clients who may
 *  not have one previously.  The canonical name is the name to be used in
 *  EVdfg_assign_node().  The only constraint on the name is that not have been 
 *  assigned to any already-participating node.
 *
 * \param master The master the client is joining.
 * \param given_name The original name of the client.
 * \param canonical_name The canonical name to be assigned to the client.
 * \return true on success, false if the name was not unique
 */
//extern int EVmaster_assign_canonical_name(EVmaster master, char *given_name, char *canonical_name);

#ifdef	__cplusplus
}
#endif

#define __EV_STORE__H__
#endif
