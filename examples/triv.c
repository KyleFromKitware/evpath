/* this file is evpath/examples/triv.c */
#include "evpath.h"

typedef struct _simple_rec {
    int integer_field;
} simple_rec, *simple_rec_ptr;

static IOField simple_field_list[] =
{
    {"integer_field", "integer", sizeof(int), IOOffset(simple_rec_ptr, integer_field)},
    {NULL, NULL, 0, 0}
};
static CMFormatRec simple_format_list[] =
{
    {"simple", simple_field_list},
    {NULL, NULL}
};

static int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    printf("I got %d\n", event->integer_field);
}

int main(int argc, char **argv)
{
    CManager cm;
    simple_rec data;
    EVstone stone;
    EVsource source;

    cm = CManager_create();
    stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm, stone, simple_format_list, simple_handler, NULL);

    source = EVcreate_submit_handle(cm, stone, simple_format_list);
    data.integer_field = 217;
    EVsubmit(source, &data, NULL);
}
