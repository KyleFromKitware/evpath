#include <stdio.h>
#include <string.h>
#include <atl.h>
#include <stdlib.h>
#include <unistd.h>
#include "Weir/weir.h"
#include "shared_code.h"

int main(int argc, char* argv[])
{
    if(argc != 2 && argc != 3)
    {
        printf("Usage: %s number_of_client_procs [size_of_inner_ring]\n", argv[0]);
        exit(1);
    }


    int groups[1] = {1};
    CManager cm;
    weir_client client;
    char * master_str;
    int number_procs = atoi(argv[1]);

    cm  = CManager_create();
    CMlisten(cm);
    CMfork_comm_thread(cm);
    weir_master master;
    if(argc == 2)
    {
	printf("2 args means using tree graph!\n");
	master = weir_master_create(cm, number_procs, weir_tree_graph);
    }
    else
    {
	printf("3 args means using ring graph!\n");
	master = weir_master_create(cm, number_procs, weir_ring_graph);
	int inner_ring_size = atoi(argv[2]);
	weir_set_ring_size(master, inner_ring_size);
    }
    
    printf("Created the master!\n");

    master_str = weir_master_get_contact_list(master);
    printf("Master contact str:%s\n", master_str);

    client = weir_client_assoc_local(cm, master, store_cod, queue_list, my_simple_callback, NULL, groups, 1);


    weir_client_ready_wait(client);

    int number_clients = weir_get_number_in_group(client);
    
    int id = weir_get_client_id_in_group(client);

    lamport_clock data;
    data.number_of_clients = number_clients;
    data.local_view = calloc(number_clients, sizeof(int));
    data.local_view[id - 1] = 1;

    weir_submit(client, &data, NULL);
    sleep(1);

    lamport_clock data2;
    data2.number_of_clients = number_clients;
    data2.local_view = calloc(number_clients, sizeof(int));
    data2.local_view[id - 1] = 2;
    weir_submit(client, &data2, NULL);

    //CMrun_network(cm);
    weir_send_shutdown_and_wait(client);

    return 0;
}
