#include "evpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

    EVstone data_stone; 
    char* data_string_list;
    EVstone split_client_data_req_stone;
    EVaction split_client_data_req_action;
 
typedef struct _simple_rec {
    int dim_size;
    char name[128];
    char addr[4096];
} simple_rec, *simple_rec_ptr;

typedef struct _domain{
    int num_dims;
    int *start;
    int *end;
} domain, sub_domain, *domain_ptr,*sub_domain_ptr;

typedef struct _proc_req{
    int num_dims;
    int *start;
    int *end;
} proc_req, *proc_req_ptr;


static FMField simple_field_list[] =
{
    {"dim_size", "integer", sizeof(int), FMOffset(simple_rec_ptr, dim_size)},
    {"name", "char[128]", sizeof(double), FMOffset(simple_rec_ptr, name)},
    {"addr", "char[4096]", sizeof(char), FMOffset(simple_rec_ptr, addr)},

    {NULL, NULL, 0, 0}
};
static FMStructDescRec simple_format_list[] =
{
    {"simple", simple_field_list, sizeof(simple_rec), NULL},
    {NULL, NULL}
};

static FMField domain_field_list[] =
{
    {"num_dims", "integer", sizeof(int), FMOffset(domain_ptr, num_dims)},
    {"start", "int[num_dims]", sizeof(int), FMOffset(domain_ptr, start)},
    {"end", "int[num_dims]", sizeof(int), FMOffset(domain_ptr, end)},
    {NULL, NULL, 0, 0}
};
static FMStructDescRec domain_format_list[] =
{
    {"domain", domain_field_list, sizeof(domain), NULL},
    {NULL, NULL}
};




typedef struct _meta_data_server{
    int num_array_dim;
    int *dim_array_size;
    int *dim_proc_size;
    int addr_size;
    char *addr;

} meta_data_server, *meta_data_server_ptr;


static FMField metadata_server_field_list[] =
{
    {"num_array_dim", "integer", sizeof(int), FMOffset(meta_data_server_ptr, num_array_dim)},
    {"dim_array_size", "integer[num_array_dim]", sizeof(int), FMOffset(meta_data_server_ptr, dim_array_size)},
    {"dim_proc_size", "integer[num_array_dim]", sizeof(int), FMOffset(meta_data_server_ptr, dim_proc_size)},
     {"addr_size", "integer", sizeof(int), FMOffset(meta_data_server_ptr, addr_size)},
   {"str", "char[addr_size]", sizeof(char), FMOffset(meta_data_server_ptr, addr)},
    {NULL, NULL, 0, 0}
};


static FMStructDescRec metadata_server_format_list[] =
{
    {"meta_server", metadata_server_field_list, sizeof(meta_data_server), NULL},
    {NULL, NULL}
};

typedef struct _meta_data_client{
    int num_array_dim;
    int *start;
    int *end;
    int length;
    char *addr;

} data_client, *data_client_ptr;

static FMField data_client_field_list[] =
{
    {"num_array_dim", "integer", sizeof(int), FMOffset(data_client_ptr, num_array_dim)},
    {"start", "integer[num_array_dim]", sizeof(int), FMOffset(data_client_ptr, start)},
    {"end", "integer[num_array_dim]", sizeof(int), FMOffset(data_client_ptr, end)},
     {"length", "integer", sizeof(int), FMOffset(data_client_ptr, length)},
  {"addr", "char[length]", sizeof(char), FMOffset(data_client_ptr, addr)},
    {NULL, NULL, 0, 0}
};


static FMStructDescRec data_client_format_list[] =
{
    {"meta_client", data_client_field_list, sizeof(data_client), NULL},
    {NULL, NULL}
};



typedef struct _array_data{
    int num_array_dim;
    int *start;
    int *end;
    int *global_dim;
    int total_local_size;
    char *value;
} array_data, *array_data_ptr;

static FMField arraydata_field_list[] =
{
    {"num_array_dim", "integer", sizeof(int), FMOffset(array_data_ptr, num_array_dim)},
    {"start", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, start)},
     {"end", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, end)},
   {"global_dim", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, global_dim)},
     {"total_local_size", "integer", sizeof(int), FMOffset(array_data_ptr, total_local_size)},
   {"value", "char[total_local_size]", sizeof(char), FMOffset(array_data_ptr, value)},
    {NULL, NULL, 0, 0}
};


static FMStructDescRec arraydata_format_list[] =
{
    {"array", arraydata_field_list, sizeof(array_data), NULL},
    {NULL, NULL}
};



enum status {success,failure} status_t;

enum status adios_ev_send(char* component_name, int processID, simple_rec_ptr multi_array, char *ev_func){


}

void updateClient2dArray(array_data_ptr client_array, array_data server_array){
  int i,j;
  int *client_element =  (int*) client_array->value;
  int *server_element = (int*) server_array.value;
  int dim_y = server_array.global_dim[1];
  int index=0;

  for (i=server_array.start[0];i<=server_array.end[0];i++){
    for (j=server_array.start[1];j<=server_array.end[1];j++){
        client_element[index] = server_element[index];
        index++;
     }
  }


}

static int array_handler(CManager cm, void *vevent, void *array_org, attr_list attrs)
{
    printf("hello from client \n");
    array_data_ptr array_ptr = vevent;
    int *get_value =(int*) array_ptr->value;
    int num_elements= 1;
    int i,j;

    int dim_x = array_ptr->global_dim[0]; 
    int dim_y = array_ptr->global_dim[1];
    printf("In data_handler dim_x =%d, dim_y=%d\n",dim_x,dim_y);
    printf("start[0]=%d, end[0]=%d, start[1]=%d, end[1]=%d\n",array_ptr->start[0],array_ptr->end[0], array_ptr->start[1], array_ptr->end[1]);

    int*  recArray = (int*) malloc(dim_x*dim_y*sizeof(int));
    for (i=0;i< dim_x*dim_y ;i++) recArray[i]=0;
    int index=0;
    domain array_domain;

    array_domain.num_dims = array_ptr->num_array_dim;
    array_domain.start = (int*) malloc(array_domain.num_dims*sizeof(int));
    array_domain.end = (int*) malloc(array_domain.num_dims*sizeof(int));
  
    for (i=0; i<array_ptr->num_array_dim;i++){
      num_elements = num_elements* (array_ptr->end[i]-array_ptr->start[i]+1);
      array_domain.start[i] = array_ptr->start[i];
      array_domain.end[i] = array_ptr->end[i];
    }
//     array1DRecover_int(*array_ptr,array_domain,recArray);


    int *element = (int*) array_ptr->value;
    printf("num element %d, total_local_size %d\n", num_elements, array_ptr->total_local_size);
//    printf("at element 5, value %d\n",element[5]);
//    for (i=0;i<num_elements;i++) printf("%d ",element[i]);
    
    printf("\n");
   index=0;
    for (i=array_ptr->start[0];i<=array_ptr->end[0];i++){
      for (j=array_ptr->start[1]; j<=array_ptr->end[1];j++){
        printf("%d ",element[index]);
        index++;
      }
      printf("\n");
    }
  
    free(recArray);
    free(array_domain.start);
    free(array_domain.end);

/*

    for (i=0;i<num_elements;i++) printf("value %d\n",get_value[i]);

    array_data_ptr org_array_ptr =(array_data_ptr) array_org;
    int *org_value = (int*) org_array_ptr->value;
//    for (i=array_ptr->start[0];i<array_ptr->end[0];i++) 
    memcpy(&org_value[array_ptr->start[0]],get_value,num_elements*sizeof(int));

     for (i=org_array_ptr->start[0];i<org_array_ptr->end[0];i++) printf("new value %d\n",org_value[i]);
*/
}

static int
metadata_request(CManager cm, void *vevent, void *array_req, attr_list attrs)
{
      int i,j;
     meta_data_server_ptr event = vevent;
    
     printf("Hello from subcriber: num_array_dim = %d, data server addr %s \n", event->num_array_dim, event->addr);
    
    printf("Array size: dim[0]=%d, dim[1]=%d. Proc size dim[0]=%d, dim[1]=%d\n", event->dim_array_size[0],event->dim_array_size[1], event->dim_proc_size[0],event->dim_proc_size[1]);
    int *domain_size= (int*) malloc (event->num_array_dim*sizeof(int));
    for (i=0;i<event->num_array_dim;i++){
      domain_size[i]= event->dim_array_size[i]/event->dim_proc_size[i];
      printf("i=%d, domain_size=%d\n",i,domain_size[i]);
    }
   
     proc_req req_procs;
    req_procs.num_dims= event->num_array_dim;
    req_procs.start =  (int*) malloc(event->num_array_dim*sizeof(int));
    req_procs.end = (int*) malloc(event->num_array_dim*sizeof(int));

    data_client_ptr data_client_req= (data_client_ptr) array_req;
    for (i=0; i < req_procs.num_dims;i++){
     req_procs.start[i] = data_client_req->start[i]/domain_size[i];
     req_procs.end[i] = data_client_req->end[i]/domain_size[i];
     printf("at i=%d, start= %d, end =%d\n", i, req_procs.start[i],req_procs.end[i]);
    }
    
//create an action when receiving data from server
  //   data_stone = EValloc_stone(cm);
    char *contact_list = (char*) malloc(4096*event->dim_array_size[0]*event->dim_array_size[1]*sizeof(char));
    char *first_contact = (char*) malloc(4096*sizeof(char));

    char *filter_contact;
    int num_contacts=1;

    for (i=0;i<req_procs.num_dims;i++) 
      num_contacts *= (req_procs.end[i]-req_procs.start[i]+1);
    printf("num contact %d\n",num_contacts);

    char **contacts = NULL, *contact_spec, *next;
    int contact_count;
    contact_spec = strchr(event->addr, ',');
    if (contact_spec != NULL) { /* if there is a filter spec */
      *contact_spec = 0;           /* terminate the contact list */
      contact_spec++;   /* advance pointer to string start */
      contacts = malloc(sizeof(contacts[0]) * 2);
      contacts[0] = event->addr;
      contact_count=1; 
      while (contact_spec != NULL) {
      next = strchr(contact_spec, ',');
        if (next != NULL) {
          *next = 0;
          next++;
      }   
      contacts = realloc(contacts, sizeof(contacts[0]) * (contact_count + 2));
      contacts[contact_count++] = contact_spec;
      contact_spec = next;
      }   
      contacts[contact_count] = NULL;

    }
    for (i=0;i<contact_count;i++){
      printf("contact i=%d is %s\n",i,contacts[i]);
    }
 // create a stone to send data request to server
    // read the address of the data handling server
    char contact_data_server[4096], data_server_string_list[4096];
    EVstone server_data_stone;
    attr_list contact_data_server_list;

    int proc_index;
    EVstone data_client_stone;
    EVsource data_source;
    int m,n;
    for (i=req_procs.start[0];i<=req_procs.end[0];i++)
    {
      for (j=req_procs.start[1];j<=req_procs.end[1];j++)
    {
        proc_index = j + i*event->dim_proc_size[1];
        printf("send request to processor %d\n",proc_index);
             sscanf(contacts[proc_index],"%d:%s",&server_data_stone,data_server_string_list);

            // create a stone to send data to data handling server
            data_client_stone = EValloc_stone(cm);
            contact_data_server_list = attr_list_from_string(data_server_string_list);
            EVassoc_bridge_action(cm,data_client_stone,contact_data_server_list,server_data_stone);
            EVaction_add_split_target(cm,split_client_data_req_stone,split_client_data_req_action,data_client_stone);
            data_source = EVcreate_submit_handle(cm, split_client_data_req_stone, data_client_format_list);

      }
    }

            EVsubmit(data_source,array_req,NULL);
/*                            free(contact_list);
    free(first_contact);    
    free(domain_size);
    free(req_procs.start);
    free(req_procs.end);
*/

}

void create2dArray_int(int *orgArray, domain orgDomain,sub_domain subDomain, array_data_ptr array_ptr  ){
//note: orgDomain.start alwa
    int i,j, index=0;
    int num_elements= (subDomain.end[0]-subDomain.start[0]+1)*(subDomain.end[1]-subDomain.start[1]+1);
    printf("num_elements %d\n",num_elements); 
    array_ptr->num_array_dim= orgDomain.num_dims;
    array_ptr->start = (int*) malloc(orgDomain.num_dims*sizeof(int));
    array_ptr->end =   (int*) malloc(orgDomain.num_dims*sizeof(int));
    array_ptr->global_dim =  (int*) malloc(orgDomain.num_dims*sizeof(int));
    array_ptr->total_local_size = num_elements*sizeof(int);
    array_ptr->value = (char*) malloc(array_ptr->total_local_size);

    for (i=0;i<orgDomain.num_dims;i++){
      array_ptr->start[i] = subDomain.start[i];
      array_ptr->end[i] = subDomain.end[i];
      array_ptr->global_dim[i]= orgDomain.end[i];
      printf("i=%d, start=%d, end=%d, global=%d \n",i,array_ptr->start[i],array_ptr->end[i],array_ptr->global_dim[i]);
    }
    
    int *element = (int*) array_ptr->value;
    for (i=subDomain.start[0];i<=subDomain.end[0];i++){
      for (j=subDomain.start[1];j<=subDomain.end[1];j++){
        element[index] = orgArray[j+i*(orgDomain.end[1]+1)];
        index++;
      }
    }
     
}

void array1DRecover_int(array_data array,  domain orgDomain,sub_domain subDomain, int* reArray ){

    int i,j,index=0;
    int *element = (int*) array.value;

    for (i=subDomain.start[0];i<=subDomain.end[0];i++){
      for (j=subDomain.start[1];j<=subDomain.end[1];j++){
//        printf("i=%d,j=%d,j+i*allDomain.end[1]+1=%d\n",i,j,j+i*(orgDomain.end[1]+1)+1);
        reArray[j+i*(orgDomain.end[1]+1)] = element[index]; 
        index++;
      }
    }
}


/* this file is evpath/examples/derived_send.c */
int main(int argc, char **argv)
{

    FILE *contact = fopen("contact.txt","r");

    EVstone meta_stone, data_stone ;
    char *meta_string_list;
    char contact_data_addr[2048];

    CManager cm;
    simple_rec data;

    int i,j;
    printf("hello\n");
   data_client array_req;

    array_data array;
    char temp_addr[2048];


    array.num_array_dim=1;
    array.start = (int*) malloc(array.num_array_dim*sizeof(int));
    array.end = (int*) malloc(array.num_array_dim*sizeof(int));
    array.global_dim = (int*) malloc(array.num_array_dim*sizeof(int));
  
    array.start[0]=0;
    array.end[0]=9;
    int num_elements = array.end[0]-array.start[0]+1;
    array.value = (char*) malloc(num_elements*sizeof(int));

    int temp[10];
    for (i=0;i<10;i++) temp[i]=0;
    memcpy(array.value,temp,10*sizeof(int));

    int dim_x=6, dim_y=6;
    int A[dim_x][dim_y], B[dim_x][dim_y];
    int *A1d= (int*) malloc(dim_x*dim_y*sizeof(int));
    int *B1d=  (int*) malloc(dim_x*dim_y*sizeof(int));

    

    domain allDomain, subDomain;
    allDomain.num_dims=2;

    allDomain.start = (int*) malloc(allDomain.num_dims*sizeof(int));
    allDomain.end = (int*) malloc(allDomain.num_dims*sizeof(int));

    allDomain.start[0]=0;
    allDomain.start[1]=0;
    allDomain.end[0]=dim_x-1;
    allDomain.end[1]=dim_y-1;

    for (i=allDomain.start[0];i<=allDomain.end[0];i++){
      for (j=allDomain.start[1];j<=allDomain.end[1];j++){
         A[i][j]= j + i*dim_y;
         A1d[j+i*dim_y] = 0;
         B[i][j]=0;
         B1d[j+i*dim_y ]=0;
         printf("%d ",A[i][j]);
          
      }
      printf("\n");
    } 

    array_data array2d;
    create2dArray_int(A1d,allDomain,allDomain,&array2d);
    data_client client_data_req;
    client_data_req.num_array_dim = allDomain.num_dims;
    client_data_req.start = (int*) malloc (client_data_req.num_array_dim*sizeof(int)  ); 
    client_data_req.end = (int*) malloc (client_data_req.num_array_dim*sizeof(int));
    client_data_req.length=4096*sizeof(char);
    client_data_req.addr = (char*) malloc (client_data_req.length);

    client_data_req.start[0]=2;
    client_data_req.end[0]=3;
    client_data_req.start[1]=2;
    client_data_req.end[1]=3;

    client_data_req.start[0]=0;
    client_data_req.end[0]=5;
    client_data_req.start[1]=0;
    client_data_req.end[1]=2;


    cm = CManager_create();
    CMlisten(cm);
if (argc==1)
{
    printf("client waiting for metadata from server\n");
//create an action when receiving data from server
   data_stone = EValloc_stone(cm);

    split_client_data_req_stone = EValloc_stone(cm);
    split_client_data_req_action = EVassoc_split_action(cm,split_client_data_req_stone,NULL);


     EVassoc_terminal_action(cm, data_stone, arraydata_format_list, array_handler,&array);
    data_string_list = attr_list_to_string(CMget_contact_list(cm));

    sprintf(client_data_req.addr,"%d:%s",data_stone,data_string_list);

// create an action when receiving metadata from server
    meta_stone = EValloc_stone(cm);
     EVassoc_terminal_action(cm, meta_stone, metadata_server_format_list, metadata_request, &client_data_req);
    meta_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Request contact list \"%d:%s\"\n", meta_stone, meta_string_list);
    FILE *client_req=fopen("client_req","w");
    fprintf(client_req, "%d:%s", meta_stone, meta_string_list);
    fclose(client_req);
 


    printf("client sending data to server\n");
// create a stone to send metadata request to server
    char contact_metadata_server[4096], metadata_server_string_list[4096];
    EVstone server_metadata_stone;
    attr_list contact_metaserver_list;

    //read address of the metadata handling server
    char *metadata_server_stone;
    FILE *metadata_server= fopen("metadata_server","r");
    fscanf(metadata_server,"%s",contact_metadata_server);
    printf("contact for metadata server %s \n", contact_metadata_server);
    sscanf(contact_metadata_server,"%d:%s",&server_metadata_stone,metadata_server_string_list);
    fclose(metadata_server);
  
    // create a stone to send request to metadata server
    EVstone metadata_client_stone = EValloc_stone(cm);
    attr_list contact_metadata_server_list = attr_list_from_string(metadata_server_string_list);
    EVassoc_bridge_action(cm, metadata_client_stone, contact_metadata_server_list, server_metadata_stone);
    EVsource metadata_source = EVcreate_submit_handle(cm,metadata_client_stone,simple_format_list);
  
    simple_rec client_request;
    client_request.dim_size=1;
    sprintf(client_request.name,"%s","H20");
    sprintf(client_request.addr,"%d:%s",meta_stone,meta_string_list);

    EVsubmit(metadata_source, &client_request, NULL);
/*

// create a stone to send data request to server
    // read the address of the data handling server
    char contact_data_server[4096], data_server_string_list[4096];
    EVstone server_data_stone;
    attr_list contact_data_server_list;

    FILE *data_server= fopen("data_server","r");
    fscanf(data_server,"%s",contact_data_server);
    printf("contact data server %s \n",contact_data_server);
    sscanf(contact_data_server,"%d:%s",&server_data_stone,data_server_string_list);
    fclose(data_server); 

    // create a stone to send data to data handling server
    EVstone data_client_stone = EValloc_stone(cm);
    contact_data_server_list = attr_list_from_string(data_server_string_list);
    EVassoc_bridge_action(cm,data_client_stone,contact_data_server_list,server_data_stone);
    EVsource data_source = EVcreate_submit_handle(cm, data_client_stone, data_client_format_list);
    data_client client_data;
    EVsubmit(data_source,&client_data,NULL);
*/
}
if (argc==2)
{

//create an action when receiving metadata from server

     meta_stone = EValloc_stone(cm);
     EVassoc_terminal_action(cm, meta_stone, metadata_server_format_list, metadata_request, &array_req);
    meta_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Request contact list \"%d:%s\"\n", meta_stone, meta_string_list);
    FILE *client_req=fopen("client_req","w");
    fprintf(client_req, "%d:%s", meta_stone, meta_string_list);
    fclose(client_req);
 


//create an action when receiving data from server
    data_stone = EValloc_stone(cm);

     EVassoc_terminal_action(cm, data_stone, arraydata_format_list, array_handler, &array);
    data_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Data contact list \"%d:%s\"\n", data_stone, data_string_list);
    FILE *client_data_contact=fopen("client_data_contact","w");

    fprintf(client_data_contact,"%d:%s", data_stone, data_string_list);

    fclose(client_data_contact);
} 
   CMsleep(cm, 600);


}
