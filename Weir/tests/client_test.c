#include <stdio.h>
#include <string.h>
#include <atl.h>
#include <stdlib.h>
#include <unistd.h>
#include "Weir/weir.h"
#include "shared_code.h"


int main(int argc, char * argv[])
{
    if(argc != 2)
    {
        printf("Usage: %s master_contact_str\n", argv[0]);
        exit(1);
    }

    int groups[1] = {1};
    CManager cm;
    cm  = CManager_create();
    CMlisten(cm);
    CMfork_comm_thread(cm);
    printf("About to enter weir_client_assoc\n");
    weir_client client = weir_client_assoc(cm, argv[1], store_cod, queue_list, my_simple_callback, NULL, groups, 1);

    weir_client_ready_wait(client);

    printf("Ready wait_finished!\n");

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
    weir_submit(client, &data, NULL);

    weir_send_shutdown_and_wait(client);

    return 0;
}
