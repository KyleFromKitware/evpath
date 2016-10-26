#include <stdio.h>
#include <string.h>
#include <atl.h>
#include <stdlib.h>
#include <unistd.h>
#include "Weir/weir.h"
#include "shared_code.h"
#include "mpi.h"

int main(int argc, char* argv[])
{
    if(argc != 1 && argc != 2)
    {
        printf("Usage: %s [size_of_inner_ring]\n", argv[0]);
        exit(1);
    }

    int rank, size;
    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);


    int groups[1] = {1};
    CManager cm;
    weir_client client;
    char * master_str;
    int number_procs = size;
    printf("Rank: %d\tNumber_clients: %d\n", rank, number_procs);

    cm  = CManager_create();
    CMlisten(cm);
    CMfork_comm_thread(cm);
    if(rank == 0)
    {
	MPI_Barrier(comm);
	weir_master master;
    	if(argc == 1)
    	{
    	    printf("1 arg means using tree graph!\n");
    	    master = weir_master_create(cm, number_procs, weir_tree_graph);
    	}
    	else
    	{
    	    printf("2 args means using ring graph!\n");
    	    master = weir_master_create(cm, number_procs, weir_ring_graph);
    	    int inner_ring_size = atoi(argv[1]);
    	    weir_set_ring_size(master, inner_ring_size);
    	}
    	
    	//printf("Created the master!\n");

    	master_str = weir_master_get_contact_list(master);
	size_t weir_master_contact_str_size = strlen(master_str) + 1;
	MPI_Bcast(&weir_master_contact_str_size, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(master_str, weir_master_contact_str_size, MPI_CHAR, 0, MPI_COMM_WORLD);
    	//printf("Master contact str:%s\n", master_str);

    	client = weir_client_assoc_local(cm, master, store_cod, queue_list, my_simple_callback, NULL, groups, 1);
    }
    else
    {
	char * master_string;
	int master_string_size;
	MPI_Barrier(comm);
	MPI_Bcast(&master_string_size, 1, MPI_INT, 0, MPI_COMM_WORLD);
	master_string = malloc(sizeof(char) * master_string_size + 1);
	MPI_Bcast(master_string, master_string_size, MPI_CHAR, 0, MPI_COMM_WORLD);
	client = weir_client_assoc(cm, master_string, store_cod, queue_list, my_simple_callback, NULL, groups, 1);
    }
    weir_client_ready_wait(client);
    MPI_Barrier(comm);
    //printf("Past the barriers\n");



    int number_clients = weir_get_number_in_group(client);
    printf("Rank: %d\tNumber_clients: %d\n", rank, number_clients);
    
    int id = weir_get_client_id_in_group(client);

    lamport_clock data;
    data.number_of_clients = number_clients;
    data.local_view = calloc(number_clients, sizeof(int));
    data.local_view[id - 1] = 1;

    weir_submit(client, &data, NULL);

    lamport_clock data2;
    data2.number_of_clients = number_clients;
    data2.local_view = calloc(number_clients, sizeof(int));
    data2.local_view[id - 1] = 2;
    weir_submit(client, &data2, NULL);

    //CMrun_network(cm);
    while(1)
    {
	sleep(1);
    }

    return 0;
}
