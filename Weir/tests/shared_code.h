#ifndef SHARED_CODE_H_WEIR
#define SHARED_CODE_H_WEIR

typedef struct _rec_a {
    int number_of_clients;
    int * local_view;
} lamport_clock, *lamport_clock_ptr;

static FMField lamport_clock_field_list[] =
{
    {"number_of_clients", "integer", sizeof(int), FMOffset(lamport_clock_ptr, number_of_clients)},
    {"local_view", "integer[number_of_clients]", sizeof(int), FMOffset(lamport_clock_ptr, local_view)},
    {NULL, NULL, 0, 0}
};

static FMStructDescRec lamport_clock_format_list[] =
{
    {"lamport_clock", lamport_clock_field_list, sizeof(lamport_clock), NULL},
    {NULL, NULL, 0, NULL}
};

static FMStructDescList queue_list[] = {lamport_clock_format_list, NULL};



static char * store_cod = "{\n\
            static int number_procs = 0;\n\
            static int local_count = 0;\n\
            static int first_time = 1;\n\
            static int * local_time;\n\
            attr_list the_event_attrs = EVget_attrs_lamport_clock(0);\n\
            int last_node_id;\n\
            int my_node_id;\n\
            last_node_id = attr_ivalue(the_event_attrs, \"last_node_id\");\n\
            my_node_id = attr_ivalue(stone_attrs, \"my_node_id\");\n\
            int i;\n\
            lamport_clock * a_ptr = EVdata_lamport_clock(0);\n\
            number_procs = a_ptr.number_of_clients;\n\
            printf(\"\%d: Number of procs: %d\\n\", my_node_id, number_procs);\n\
            if(first_time)\n\
            {\n\
                local_time = malloc(sizeof(int) * number_procs);\n\
                for(i = 0; i < number_procs; i++)\n\
                {\n\
                    local_time[i] = a_ptr->local_view[i];\n\
                }\n\
                first_time = 0;\n\
            }\n\
            else\n\
            {\n\
                lamport_clock * a_ptr = EVdata_lamport_clock(0);\n\
                for(i = 0; i < number_procs; i++)\n\
                {\n\
                    if(a_ptr->local_view[i] > local_time[i])\n\
                    {\n\
			local_time[i] = a_ptr->local_view[i];\n\
                    }\n\
                }\n\
            }\n\
            lamport_clock to_send;\n\
            to_send.number_of_clients = number_procs;\n\
            for(i = 0; i < number_procs; i++)\n\
            {\n\
                to_send->local_view[i] = local_time[i];\n	\
            }\n\
            if(my_node_id == last_node_id)\n\
            {\n\
                local_count = local_count + 1;\n\
                if(local_count == 1)\n\
                {\n\
		    int port = weir_get_port(the_event_attrs);\n\
		    set_int_attr(the_event_attrs, \"last_node_id\", my_node_id);\n\
		    EVsubmit_attr(port, to_send, the_event_attrs);\n\
                }\n\
                else \n\
                {\n\
                    printf(\"\%d: Submitting to local only!\\n\", my_node_id);\n\
                    EVsubmit(0, to_send);\n\
                }\n\
                EVdiscard_lamport_clock(0);\n\
            }\n\
            else\n\
            {\n\
                printf(\"\%d: Submitting to the tree!\\n\", my_node_id);\n\
		int port = weir_get_port(the_event_attrs);\n\
		set_int_attr(the_event_attrs, \"last_node_id\", my_node_id);\n\
                EVsubmit_attr(port, to_send, the_event_attrs);\n\
                EVsubmit(0, to_send);\n\
                EVdiscard_lamport_clock(0);\n\
            }\n\
        }\0\0";


int
my_simple_callback (CManager cm, void * vevent, void * client_data, attr_list attrs)
{
    lamport_clock_ptr event = (lamport_clock_ptr) vevent;
    int i;
    printf("Value of the clock: ");
    for(i = 0; i < event->number_of_clients; i++)
    {
        printf("%d ", event->local_view[i]);
    }
    printf("\n");

    return 1;
}

#endif
