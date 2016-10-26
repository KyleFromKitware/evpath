#ifndef _DIFF_IMPLEMENTATIONS_H
#define _DIFF_IMPLEMENTATIONS_H

/* This file is seperating out different implementations of the research being done to 
** analyze the best method to distribute and set off the shared global data chain.
** This file is placed in ev_store.c at a weird time because we have global static variables
** that we want to use...forgive me for wanting to separate out some of the madness.*/

// The ASSIGN_IMPL_SPECIFIC_FUNCTIONS gives the template for implementation functions

/*The tree implementation*/

static void
create_weir_tree_graph(weir_group_info_ptr info, weir_graph_node_ptr* graph, weir_master master)
{
    *graph = calloc(info->number_of_participants, sizeof(weir_graph_node));
    int i, adjusted_index;

    for(i = 0; i < info->number_of_participants; i++)
    {
        adjusted_index = i + 1;

        (*graph)[i].str_contact = malloc(sizeof(char) * 75);
	sprintf((*graph)[i].str_contact, "%d:%s", info->local_stone_ids[i], info->str_contact_names[i]);
        (*graph)[i].node_group_id = adjusted_index;

        if(i == 0)
        {
            (*graph)[i].number_of_producers = 0;
            (*graph)[i].producers = NULL;
        }
        else
        {
            (*graph)[i].number_of_producers = 1;
            int adjusted_parent_index = adjusted_index / 2;
            int parent_index = adjusted_parent_index - 1;
            (*graph)[i].producers = calloc(1, sizeof(weir_graph_node_ptr));
            (*graph)[i].producers[0] = (*graph) + parent_index;
        }


        if(2*adjusted_index <= info->number_of_participants)
        {
            (*graph)[i].number_of_consumers++;
            (*graph)[i].consumers = realloc((*graph)[i].consumers, sizeof(weir_graph_node_ptr) * (*graph)[i].number_of_consumers);
            (*graph)[i].consumers[(*graph)[i].number_of_consumers - 1] = (*graph) + 2 * adjusted_index - 1;
        }

        if(2*adjusted_index + 1 <= info->number_of_participants)
        {
            (*graph)[i].number_of_consumers++;
            (*graph)[i].consumers = realloc((*graph)[i].consumers, sizeof(weir_graph_node_ptr) * (*graph)[i].number_of_consumers);
            (*graph)[i].consumers[(*graph)[i].number_of_consumers - 1] = (*graph) + 2 * adjusted_index;
        }

    }
    
}

/* Notes for the tree:
    1) Two types of messages may be incoming on submit. a) From local or b) From outside
    2) Local messages will need a check to see if they are allowed to submit to the chain
    3) Outside messages should have their attr changed and then forwarded correctly
    4) It will be up to the cod code to determine the frequency of the submits, which means that the 
       cod code will need to check the attr_list and weir_submit if the incoming node_id doesn't match 
       the client's node id.
*/
static int get_port_weir_tree_graph(weir_client client, attr_list attrs)
{
    assert(CManager_locked(client->cm));
    int data_from;
    static int root_timestep = 0;
    static int received_from_consumer = 0;
    if(!get_int_attr(attrs, LAST_NODE_ID, &data_from))
    {
        fprintf(stderr, "Error: LAST_NODE_ID attr get failed in cod_weir_submit!\n");
    }
    int my_id = client->my_node_id;
    if(my_id == data_from)
    {
        //Local message, check if designated node
        if(my_id == 1)
        {
            //Designated node, start the chain
            set_int_attr(attrs, WEIR_TIMESTEP, root_timestep);
	    root_timestep++;
            client->number_of_msgs_in_subgraph += client->number_of_consumer_bridges;
	    //CMtrace_out(client->cm, weirVerbose, "Number_of_msgs_in_subgraph: %d\n", client->number_of_msgs_in_subgraph);
	    weir_verbose(client, "Number_of_msgs_in_subgraph: %d\n", client->number_of_msgs_in_subgraph);
            return 1;
            //INT_EVsubmit(client->to_consumer_source, data, attrs);
        }
        else
        {
            //Do nothing, not a designated node
            return 0;
        }
    }
    else //Non-local message forward it to the producer if from consumer, forward it to consumer if from producer, stop if root
    {
        int timestep_received = -1;
        get_int_attr(attrs, WEIR_TIMESTEP, &timestep_received);
        if(my_id == 1)
        {
            //Do nothing, we are root receiving final message of previous chain
            client->number_of_msgs_in_subgraph -= 1;
	    CMtrace_out(client->cm, weirVerbose, "Number_of_msgs_in_subgraph: %d\ttimestep_received: %d\n", client->number_of_msgs_in_subgraph,
			timestep_received);
	    if(client->number_of_msgs_in_subgraph < 0)
	    {
		fprintf(stderr, "Error: number of msgs in subgraph is: %d\n", client->number_of_msgs_in_subgraph);
	    }
            return 0;
        }
        else if(my_id == (data_from / 2)) //We received message from a consumer, sending it up to the producer
        {
	    received_from_consumer++;
            client->number_of_msgs_in_subgraph -= 1;
	    if(client->number_of_msgs_in_subgraph < 0)
	    {
		fprintf(stderr, "Error: number of msgs in subgraph is: %d\n", client->number_of_msgs_in_subgraph);
	    }
	    if((received_from_consumer % client->number_of_consumer_bridges) == 0)  //Here we are aggregating messages in the middle of the tree
	    {
		weir_verbose(client, "Receiving timestep: %d, submitting to producer\n", timestep_received);
		return 2;
	    }
	    else
	    {
		weir_verbose(client, "Receiving timestep: %d, waiting for next one\n", timestep_received);
		return 0;
	    }
        }
        else if((my_id == (2*data_from)) || my_id == (2*data_from + 1)) // We received message from a producer, sending to consumers
        {
            //If we are a leaf, turn around
            if(client->number_of_consumer_bridges == 0)
            {
		weir_verbose(client, "Receiving timestep: %d, we are a leaf submitting back up the tree\n", timestep_received);
		return 2;
            }
            else //Not a leaf, keep sending to consumers
            {
		weir_verbose(client, "Receiving timestep: %d, submitting to subtree!\n", timestep_received);
		client->number_of_msgs_in_subgraph += client->number_of_consumer_bridges;
		return 1;
            }
        }
        else
        {
            fprintf(stderr, "Major error!!: The tree's flow of messages is unexpected.  My id is: %d and received id is: %d\n", 
                            my_id, data_from);
            return -1;
        }
    }

}


/* This is where the ring example goes */
static void
create_weir_ring_graph(weir_group_info_ptr info, weir_graph_node_ptr* graph, weir_master master)
{
    *graph = calloc(info->number_of_participants, sizeof(weir_graph_node));
    int i;

    if(master->ring_inner_size == 0 || master->ring_inner_size > info->number_of_participants)
    {
        fprintf(stderr, "Error: ring_inner_size inappropriately set.  ring_inner_size: %d\t\tnum_participants_in_group: %d\n",
                        master->ring_inner_size, info->number_of_participants);
        return;
    }

    for(i = 0; i < info->number_of_participants; i++)
    {
        (*graph)[i].str_contact = malloc(sizeof(char) * 75);
	sprintf((*graph)[i].str_contact, "%d:%s", info->local_stone_ids[i], info->str_contact_names[i]);
        (*graph)[i].node_group_id = i + 1; //The tree is an easier implementation when the nodes are 1 indexed

        (*graph)[i].number_of_producers = 0;
        (*graph)[i].producers = NULL;
    }

    int outer_group_size = info->number_of_participants / master->ring_inner_size;
    int extra_line = info->number_of_participants % master->ring_inner_size;
    int j;
    //The first n elements of the array are the inner ring
    for(i = 0; i < master->ring_inner_size; i++)
    {
        (*graph)[i].number_of_consumers++;
        (*graph)[i].consumers = calloc(1, sizeof(weir_graph_node_ptr));
        if(i != master->ring_inner_size - 1)
            (*graph)[i].consumers[0] = (*graph) + i + 1;
        else //Wrap around to the beginning
            (*graph)[i].consumers[0] = (*graph);
    }

    //Create the outer loops
    int next_unused_node = master->ring_inner_size;
    for(i = 0; i < master->ring_inner_size; i++)
    {
        int iteration_size;
        if(i < extra_line)
            iteration_size = outer_group_size;
        else
            iteration_size = outer_group_size - 1;

        weir_graph_node_ptr previous_node = (*graph) + i;
        for(j = 0; j < iteration_size; j++)
        {
            previous_node->number_of_consumers++;
            previous_node->consumers = realloc(previous_node->consumers, sizeof(weir_graph_node_ptr) * previous_node->number_of_consumers);
            previous_node->consumers[previous_node->number_of_consumers - 1] = (*graph) + next_unused_node;
            previous_node = (*graph) + next_unused_node;
            next_unused_node++;
        }
	
	//Chain back around to the beginning if you are not alone in your outer group
	if(previous_node->number_of_consumers != 1)
	{
	    previous_node->number_of_consumers++;
	    previous_node->consumers = realloc(previous_node->consumers, sizeof(weir_graph_node_ptr) * previous_node->number_of_consumers);
	    previous_node->consumers[previous_node->number_of_consumers - 1] = (*graph) + i;
	}
    } 
}

/* Ring case:
   1) The only node that will be able to start a chain will be the first node, node 0
   2) We will use the LAST_NODE_ID to identify the "timestep" of the message
   3) Nodes will only forward to their consumers if they haven't seen the "timestep"
   4) This means that the data_from variable is actually the "timestep" variable
*/
static int get_port_weir_ring_graph(weir_client client, attr_list attrs)
{
    static int last_seen_timestep = -1;
    static int root_timestep = 0;
    int my_id = client->my_node_id;
    int timestep_of_message;
    int data_from;
    if(!get_int_attr(attrs, LAST_NODE_ID, &data_from))
    {
        fprintf(stderr, "Error: LAST_NODE_ID attr get failed in get_port_weir_ring_graph!\n");
    }
    if(!get_int_attr(attrs, WEIR_TIMESTEP, &timestep_of_message))
    {
	fprintf(stderr, "Error: WEIR_TIMESTEP attr get failed in get_port_weir_ring_graph!\n");
    }

    if(my_id == data_from)
    {
        //Local message, check if designated node
        if(my_id == 1)
        {
            //Designated node, start the chain
            //INT_EVsubmit(client->to_consumer_source, data, attrs);
            set_int_attr(attrs, WEIR_TIMESTEP, root_timestep);
	    last_seen_timestep = root_timestep;
	    root_timestep++;
            client->number_of_msgs_in_subgraph += client->number_of_consumer_bridges;
            return 1;
        }
        else
        {
            //Do nothing, not a designated node
            return 0;
        }
    }
    else //Non-local message, check timestep and forward if not seen
    {
        if(timestep_of_message <= last_seen_timestep)
        {
            //We've seen this already, do nothing, protecting from the first message
            if(client->number_of_msgs_in_subgraph != 0)
	    {
		client->number_of_msgs_in_subgraph -= 1;
	    }
            return 0;
        }
        else
        {
            last_seen_timestep = timestep_of_message;
            //INT_EVsubmit(client->to_consumer_source, data, attrs);
            return 1;
        }

    }
    return -1;

}

#endif
