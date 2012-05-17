#include "evpath.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

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

meta_data_server array_info;

    EVstone split_req_stone;
    EVaction split_req_action;

    EVstone split_handler_stone;
    EVaction split_handler_action;

    EVstone data_server_stone ;
    char *data_server_string_list ;

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


typedef struct _data_client{
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
void copy2dDomainData_int(array_data orgArray, domain reqDomain, array_data_ptr reqArray ){
//   domain uDomain;
    int i,j;
    reqArray->num_array_dim = orgArray.num_array_dim;
    reqArray->start = (int*) malloc (reqArray->num_array_dim*sizeof(int));
    reqArray->end = (int*) malloc (reqArray->num_array_dim*sizeof(int));
    reqArray->global_dim = (int*) malloc(reqArray->num_array_dim*sizeof(int));
    for (i=0;i<reqArray->num_array_dim;i++){
      if (reqDomain.start[i]<orgArray.start[i]) reqArray->start[i]=orgArray.start[i];
      else reqArray->start[i] = reqDomain.start[i];
      if (reqDomain.end[i] < orgArray.end[i]) reqArray->end[i] = reqDomain.end[i];
      else reqArray->end[i] = orgArray.end[i];
      if (reqArray->end[i] < orgArray.start[i] || reqArray->start[i]> orgArray.end[i]) {
        printf("The request is out of region\n");
        // Mark this as an invalid request
        reqArray->end[i]= reqArray->start[i]-1;
      //  return;
      }
      reqArray->global_dim[i]= orgArray.global_dim[i]; 
    }
   int num_elements = (reqArray->end[0]-reqArray->start[0]+1)*(reqArray->end[1]-reqArray->start[1]+1);
   reqArray->total_local_size = num_elements*sizeof(int);
   reqArray->value = (char*) malloc(reqArray->total_local_size);
 
   int *org_element = (int*) orgArray.value;
   int *req_element = (int*) reqArray->value;
   int index=0;
   int org_array_start_global_index = orgArray.start[1]+orgArray.start[0]*orgArray.global_dim[0];
   int req_array_start_global_index = reqArray->start[1] + reqArray->start[0]*reqArray->global_dim[0];
   int req_local_index_start = req_array_start_global_index - org_array_start_global_index;
    int req_local_index, i_index=0;
   for (i=reqArray->start[0];i<=reqArray->end[0];i++){
     req_local_index = req_local_index_start + i_index*(orgArray.end[1]-orgArray.start[1]+1);
     for (j=reqArray->start[1];j<=reqArray->end[1];j++){
        req_element[index] = org_element[req_local_index];
        index++;
        req_local_index++;
      }
      i_index++;
    }


}

void freeDomainData_int(array_data_ptr orgArray){
    free(orgArray->start);
    free(orgArray->end);
    free(orgArray->global_dim);
    free(orgArray->value);
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
      array_ptr->global_dim[i]= orgDomain.end[i] +1;
      printf("i=%d, start=%d, end=%d, global_dim=%d \n",i,array_ptr->start[i],array_ptr->end[i],array_ptr->global_dim[i]);
    }
    
    int *element = (int*) array_ptr->value;
    for (i=subDomain.start[0];i<=subDomain.end[0];i++){
      for (j=subDomain.start[1];j<=subDomain.end[1];j++){
        element[index] = orgArray[j+i*(orgDomain.end[1]+1)];
        index++;
      }
    }
     
}

void array1DRecover_int(array_data array,  sub_domain subDomain, int* reArray ){

    int i,j,index=0;
    int *element = (int*) array.value;
    int dim_y = array.global_dim[1];
    for (i=subDomain.start[0];i<=subDomain.end[0];i++){
      for (j=subDomain.start[1];j<=subDomain.end[1];j++){
//        printf("i=%d,j=%d,j+i*allDomain.end[1]+1=%d\n",i,j,j+i*(orgDomain.end[1]+1)+1);
        reArray[j+i*(dim_y)] = element[index]; 
        index++;
      }
    }
}

void subDomainSelection_int(int *orgArray, domain orgDomain,sub_domain subDomain, int* subArray  ){
//note: orgDomain.start always index at 0
    int i,j;
   printf("subDomain.start[0]=%d, subDomain.end[0]=%d\n",subDomain.start[0],subDomain.end[0]);
    printf("subDomain.start[1]=%d, subDomain.end[1]=%d\n",subDomain.start[1],subDomain.end[1]);

    for (i=subDomain.start[0];i<=subDomain.end[0];i++){
      for (j=subDomain.start[1];j<=subDomain.end[1];j++){
//        printf("i=%d,j=%d,j+i*allDomain.end[1]+1=%d\n",i,j,j+i*(orgDomain.end[1]+1)+1);
        subArray[j+i*(orgDomain.end[1]+1 ) ] = orgArray[j+i*(orgDomain.end[1]+1)];
      }
    }
     

}

void subDomainCut(int *orgArray, domain orgDomain, sub_domain subDomain, int *subArray){
//create subArray in 1D that has only the elements of sub array of array orgArray
 //   int *subArray= *sub_array;
    int index=0,i,j;
    for (i=subDomain.start[0];i<=subDomain.end[0];i++){
      for (j=subDomain.start[1];j<=subDomain.end[1];j++){
//        printf("i=%d,j=%d,j+i*allDomain.end[1]+1=%d\n",i,j,j+i*(orgDomain.end[1]+1)+1);
        subArray[index] = orgArray[j+i*(orgDomain.end[1]+1)];
        index++;
      }
    }

}
void subDomainRecover(int *subArray, domain orgDomain, sub_domain subDomain, int *reArray){
//recover multi dim reArray from 1D subArray
    int index=0,i,j;
    int dim_x = subDomain.end[0] - subDomain.start[0]+1;
    int dim_y = subDomain.end[1] - subDomain.end[1] +1;

    for (i=subDomain.start[0];i<=subDomain.end[0];i++){
      for (j=subDomain.start[1];j<=subDomain.end[1];j++){
//        printf("i=%d,j=%d,j+i*allDomain.end[1]+1=%d\n",i,j,j+i*(orgDomain.end[1]+1)+1);
        reArray[j+i*(orgDomain.end[1]+1)] = subArray[index]; 
        index++;
      }
    }
}

static int
data_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    printf("rank %d is processing request\n",rank);
     
     int i,j;
    printf("hello from data handler on server side \n");
    data_client_ptr array_req = vevent;
//    printf("num_array_dim=%d, array_req.start=%d, array_req.end=%d, array_req.addr=%s\n", array_req->num_array_dim, array_req->start[0], array_req->end[0],array_req->addr);
    printf("num_array_dim=%d\n", array_req->num_array_dim);
    for (i=0; i <array_req->num_array_dim; i++){
      printf("dim i=%d, start=%d, end=%d\n",i,array_req->start[i],array_req->end[i]);
    } 
    EVstone data_stone;
    EVstone client_data_stone;

    char data_string_list[2048];
    char contact_data_addr[2048];
    attr_list contact_data_list;
    EVsource array_data_source;

   int num_elements = 1;
 
    domain array_domain, all_domain;
    array_domain.num_dims = array_req->num_array_dim;
    array_domain.start = (int*) malloc(array_domain.num_dims*sizeof(int));
    array_domain.end = (int*) malloc(array_domain.num_dims*sizeof(int));
    
    all_domain.num_dims = array_req->num_array_dim;
    all_domain.start = (int*) malloc(all_domain.num_dims*sizeof(int));
    all_domain.end = (int*) malloc(all_domain.num_dims*sizeof(int));
 
    for (i=0; i<array_req->num_array_dim;i++){
     num_elements = num_elements* (array_req->end[i]-array_req->start[i]+1);
      array_domain.start[i] = array_req->start[i];
      array_domain.end[i] = array_req->end[i];
    }

     array_data_ptr wholeArray = (array_data_ptr) client_data;

    array_data checkArray = *wholeArray;
    printf("check array, global_dim[0]=%d, global_dim[1]=%d\n",checkArray.global_dim[0],checkArray.global_dim[1]);

    int dim_x = wholeArray->global_dim[0]; 
    int dim_y = wholeArray->global_dim[1];
    printf("In data_handler dim_x =%d, dim_y=%d\n",dim_x,dim_y);


      int*  recArray = (int*) malloc(dim_x*dim_y*sizeof(int));
    for (i=0;i< dim_x*dim_y ;i++) recArray[i]=0;

    int *check_value;
    array_data subArray;    
    copy2dDomainData_int(*wholeArray, array_domain,&subArray); 

 //   array1DRecover_int(*wholeArray,array_domain,recArray);
//     array1DRecover_int(subArray,array_domain,recArray);
      int *element =(int*) subArray.value;
      num_elements = (subArray.end[0]-subArray.start[0]+1)*(subArray.end[1]-subArray.start[1]+1);
     if (element!=NULL)  for (i=0; i <num_elements;i++) printf("%d ", element[i]);

    printf("server sending data back to client \n");
    // sending the data back to client
    // read the data connection file
/*    FILE *client_data_contact=fopen("client_data_contact","r");
    fscanf(client_data_contact,"%s", contact_data_addr);
    printf("contact for data %s\n",contact_data_addr);*/
    sscanf(array_req->addr,"%d:%s",&client_data_stone,data_string_list);

//  create a stone to send the data back to client
    data_stone = EValloc_stone(cm);
    contact_data_list = attr_list_from_string(data_string_list );
    EVassoc_bridge_action(cm,data_stone,contact_data_list,client_data_stone);
    EVaction_add_split_target(cm,split_handler_stone,split_handler_action,data_stone);


    array_data_source = EVcreate_submit_handle(cm,split_handler_stone,arraydata_format_list);
//    EVsubmit(array_data_source,&array, NULL);
    EVsubmit(array_data_source,&subArray, NULL);

//    EVsubmit(array_data_source,client_data, NULL);
    printf("\n\n\n\n END \n\n\n\n");
//    fclose(client_data_contact);
    free(array_domain.start);
    free(array_domain.end);


    free(recArray);
    freeDomainData_int(&subArray);
 }



static int
request_handler(CManager cm, void *vevent, void *metadata_server, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    printf("hello from request handler on server side\n");   

    printf("I got %d, name %s, addr %s\n", event->dim_size,event->name,event->addr);
    int i,j;

    EVstone client_req_stone, meta_stone;

    char meta_string_list[2048];
    char contact_data_req[2048];
    attr_list contact_req_list;

    EVsource metadata_source;

    sscanf(event->addr,"%d:%s",&client_req_stone,meta_string_list);
    printf("client_req_stone %d, meta_string_list %s\n",client_req_stone,meta_string_list); 
// create a stone to send metadata back to client
    meta_stone = EValloc_stone(cm);
    contact_req_list = attr_list_from_string(meta_string_list);
    EVassoc_bridge_action(cm,meta_stone,contact_req_list,client_req_stone);
    
    EVaction_add_split_target(cm,split_req_stone,split_req_action,meta_stone);
 
    metadata_source = EVcreate_submit_handle(cm,split_req_stone, metadata_server_format_list);
    meta_data_server_ptr meta_array = (meta_data_server_ptr) metadata_server;
    printf("dim_x=%d,dim_y=%d, num_array_dim=%d, dim_proc_size[0]=%d, dim_proc_size[1]=%d, meta_array.addr=%s \n", meta_array->dim_array_size[0], meta_array->dim_array_size[1], meta_array->num_array_dim, meta_array->dim_proc_size[0], meta_array->dim_proc_size[1], meta_array->addr);
//    EVsubmit(metadata_source,&data, NULL);
    EVsubmit(metadata_source,metadata_server, NULL);

}


/* this file is evpath/examples/derived_recv.c */
int main(int argc, char **argv)
{
    int rank,size;
    MPI_Init(&argc,&argv);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD,&size);
    printf("\n\n\n Hello world form process %d of %d\n",rank,size);

    int dim_x=6, dim_y=6;
    int dim_proc_x, dim_proc_y;
    dim_proc_x = dim_x/ size;
    dim_proc_y = dim_y;


    int A[dim_x][dim_y], B[dim_x][dim_y];
    int *A1d= (int*) malloc(dim_x*dim_y*sizeof(int));
    int *B1d=  (int*) malloc(dim_x*dim_y*sizeof(int));

    int *Aproc = (int*) malloc(dim_proc_x*dim_proc_y*sizeof(int));
   

    int i,j;
/*    printf("proc %d initialize data\n", rank);
    for (i=0;i< dim_proc_x;i++){
      for (j=0;j<dim_proc_y;j++){
        Aproc[j + i*dim_proc_y] =  j+ (i+rank*dim_proc_x)*dim_y;
        printf("%d ", Aproc[j+i*dim_proc_y]);
      }
      printf("\n");
    }   

     
    printf("\n\n\n\n");
*/
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
         A1d[j+i*dim_y] = A[i][j];
//         printf("%d ",A1d[ j+i*dim_y  ]);
          
      }
//      printf("\n");
    } 


    subDomain.start = (int*) malloc(allDomain.num_dims*sizeof(int));
    subDomain.end = (int*) malloc(allDomain.num_dims*sizeof(int));



    int *cutArray,*recArray;


    subDomain.start[0]=0;
    subDomain.end[0]=2;
    subDomain.start[1]=0;
    subDomain.end[1]=5;
    int numSubArrayElements= (subDomain.end[0]-subDomain.start[0]+1)*(subDomain.end[1] - subDomain.start[1]+1);

    
    
    subDomain.start[0]=3;
    subDomain.end[0]=5;
    subDomain.start[1]=0;
    subDomain.end[1]=5;
 
    recArray = (int*) malloc(dim_x*dim_y*sizeof(int));
    for (i=0;i< dim_x*dim_y ;i++) recArray[i]=0;

    array_data subArray, wholeArray;
    subDomain.start[0]=rank*dim_proc_x;
    subDomain.end[0]=(rank+1)*dim_proc_x-1;
    subDomain.start[1]=0;
    subDomain.end[1]=5;
    create2dArray_int(A1d,allDomain,allDomain,&wholeArray);

    copy2dDomainData_int(wholeArray, subDomain,&subArray); 

     array1DRecover_int(subArray,subDomain,recArray);
/*   for (i=0; i <numSubArrayElements;i++) printf("%d ", check_value[i]);
    printf("\n\n Second sub array \n\n");
    for (i=allDomain.start[0];i<=allDomain.end[0];i++){
      for (j=allDomain.start[1];j<=allDomain.end[1];j++){
        printf("%d ",recArray[j+ i*(allDomain.end[1]+1) ]); 
//        printf("%d ",check_value[j+ i*(allDomain.end[1]+1) ]); 

//        printf("%d ",B[i][j]); 
      }
      printf("\n");
    }
*/
    int *check = subArray.value; 
    int index=0;
    printf("rank =%d \n", rank);
    for (i=subArray.start[0];i<=subArray.end[0];i++){
      for (j=subArray.start[1]; j<=subArray.end[1];j++){
        printf("%d ",check[index]);
        index++;
      }
      printf("\n");
    }

    meta_data_server meta_array;
    meta_array.num_array_dim = allDomain.num_dims;
    meta_array.dim_array_size = (int*) malloc (meta_array.num_array_dim*sizeof(int));
    meta_array.dim_proc_size =   (int*) malloc (meta_array.num_array_dim*sizeof(int));

//just create 2D array
    meta_array.dim_array_size[0]=dim_x;
    meta_array.dim_array_size[1]=dim_y;
    
    meta_array.dim_proc_size[0]=size;
    meta_array.dim_proc_size[1]=1;

    meta_array.addr_size = 4096*(meta_array.dim_proc_size[0]*meta_array.dim_proc_size[1])*sizeof(char);
    meta_array.addr = (char*) malloc(meta_array.addr_size);

   
 
    CManager cm;

    char meta_string_list[2048], data_string_list[2048];
    char contact_data_addr[2048],contact_data_req[2048];
    attr_list contact_data_list, contact_req_list;
    EVsource metadata_source, array_data_source;


    cm = CManager_create();
    CMlisten(cm);

   
    printf("server waiting for metadata from client\n");

;    split_req_stone = EValloc_stone(cm);
    split_req_action = EVassoc_split_action(cm,split_req_stone,NULL);

    split_handler_stone = EValloc_stone(cm);
    split_handler_action = EVassoc_split_action(cm,split_handler_stone,NULL);

// create an action when receiving data requesting from client
    data_server_stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm,data_server_stone, data_client_format_list, data_handler, &subArray);
    data_server_string_list = attr_list_to_string(CMget_contact_list(cm));
    char data_server_file_name[128];
    sprintf(data_server_file_name,"%s_%d","data_server",rank);
    FILE *data_server = fopen(data_server_file_name,"w");
    fprintf(data_server,"%d:%s",data_server_stone,data_server_string_list);
    printf("data server %d: %d:%s\n",rank, data_server_stone,data_server_string_list);
    fclose(data_server);

    char server_addr[2048];
    sprintf(server_addr,"%d:%s",data_server_stone,data_server_string_list);  
    sprintf(meta_array.addr,"%s",server_addr);

    int *displs,*rcounts;
    displs = (int*) malloc(size*sizeof(int));
    rcounts = (int*) malloc(size*sizeof(int));
    for (i=0;i<size;i++){
      displs[i] = i*4096;
      rcounts[i]=2048;
    }
    char *addr_list = (char*) malloc(4096*size*sizeof(char));

    MPI_Gatherv(server_addr,2048,MPI_CHAR,addr_list,rcounts,displs,MPI_CHAR,0,MPI_COMM_WORLD);
  
    char **addr = (char**) malloc(size*sizeof(char*));
   index=0;
   if (!rank){

     for (i=1;i<size;i++){
        addr[i]= &addr_list[4096*i];
        printf("index=%d, addr =%s\n",i, addr[i]);
        sprintf(meta_array.addr,"%s,%s",meta_array.addr,addr[i]);
      }
     printf("string cat %s from proc %s\n", meta_array.addr, rank);

// create an action when receiving metadata requesting from client
     EVstone meta_server_stone = EValloc_stone(cm);
     EVassoc_terminal_action(cm, meta_server_stone, simple_format_list, request_handler, &meta_array);
     char *meta_server_string_list = attr_list_to_string(CMget_contact_list(cm));
     FILE *metadata_server = fopen("metadata_server","w");
     fprintf(metadata_server,"%d:%s",meta_server_stone, meta_server_string_list);
     printf("metadata server %d:%s\n",meta_server_stone, meta_server_string_list);
     fclose(metadata_server);

   }
  
    CMsleep(cm, 600);
    
   MPI_Finalize();
}
