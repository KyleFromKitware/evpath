/*
 * CM transport for inter-process communication through shared memory.
 * It is implemented with DataFabrics shared memory IPC library.
 */

/***** Includes *****/
#include "config.h"
#include <sys/types.h>

#ifdef HAVE_WINDOWS_H

#error "Error: CM Shm transport does not support Windows"

#else
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_TIMES_H
#include <sys/times.h>
#endif
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_HOSTLIB_H
#include "hostLib.h"
#endif
#ifdef HAVE_STREAMS_UN_H
#include <streams/un.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#endif
#include <stdio.h>
#include <fcntl.h>
#ifndef HAVE_WINDOWS_H
#include <net/if.h>
#include <sys/ioctl.h>
#include <errno.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif

#include <atl.h>
#include <cercs_env.h>
#include "evpath.h"
#include "cm_transport.h"

/* DataFabrics header file */
#include "df_shm.h"
#include "df_shm_queue.h"

/* additional header files needed */
#include <strings.h>
#include <pthread.h>

/* default config parameters */
#define MAX_NUM_SLOTS      10
#define MAX_SMALL_MSG_SIZE 2048
#define MAX_PAYLOAD_SIZE   2048

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#if defined (__INTEL_COMPILER)
#  pragma warning (disable: 869)
#  pragma warning (disable: 310)
#  pragma warning (disable: 1418)
#  pragma warning (disable: 180)
#  pragma warning (disable: 177)
#  pragma warning (disable: 2259)
#  pragma warning (disable: 981)
#endif

struct shm_connection_data;

static atom_t CM_SHM_FILENAME = -1;
static atom_t CM_SHM_PID = -1;
static atom_t CM_SHM_NUM_SLOTS = -1;
static atom_t CM_SHM_MAX_PAYLOAD = -1;
static atom_t CM_SHM_LISTEN_THREAD = -1;
static atom_t CM_TRANSPORT = -1;

typedef struct shm_transport_data {
    CManager cm;
    CMtrans_services svc;
    pid_t my_pid;               // local process's pid
    int socket_fd;              // local unix domain socket to accept contention messages
    char *filename;             // file path associated with socket
    df_shm_method_t shm_method; // a handle to underlying shm method
    pthread_t listen_thread;
    int listen_thread_cmd;      // 0: wait to start; 1: run; 2: stop
    struct shm_connection_data *connections;
} *shm_transport_data_ptr;

typedef struct shm_connection_data {
    CMbuffer read_buffer;
    int read_buf_len;
    shm_transport_data_ptr shm_td;
    CMConnection conn;
    attr_list attrs;
    struct sockaddr_un dest_addr;
    char *filename;              // filename for unix domain socket
    pid_t peer_pid;                 // peer process's pid
    df_shm_region_t shm_region;  // shm region for this connection
    df_shm_queue_ep_t send_ep;   // sender endpoint handle for outgoing queue
    df_shm_queue_ep_t recv_ep;   // receiver endpoint handle for incoming queue    
    struct shm_connection_data *next;
} *shm_conn_data_ptr;

typedef struct listen_struct {
  shm_transport_data_ptr shm_td;
  CMtrans_services svc;
  transport_entry trans;
} *listen_struct_p;

#ifdef WSAEWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EAGAIN WSAEINPROGRESS
#define EINTR WSAEINTR
#define errno GetLastError()
#define read(fd, buf, len) recv(fd, buf, len, 0)
#define write(fd, buf, len) send(fd, buf, len, 0)
#endif

static shm_conn_data_ptr
create_shm_conn_data(svc)
CMtrans_services svc;
{
    shm_conn_data_ptr shm_conn_data =
    svc->malloc_func(sizeof(struct shm_connection_data));
    memset(shm_conn_data, 0, sizeof(struct shm_connection_data));
    return shm_conn_data;
}

static void
add_connection(shm_transport_data_ptr shm_td, shm_conn_data_ptr shm_cd)
{
    shm_conn_data_ptr tmp = shm_td->connections;
    shm_td->connections = shm_cd;
    shm_cd->next = tmp;
}

static void
unlink_connection(shm_transport_data_ptr utd, shm_conn_data_ptr ucd)
{
    if (shm_td->connections == shm_cd) {
    shm_td->connections = shm_cd->next;
    shm_cd->next = NULL;
    } else {
    shm_conn_data_ptr tmp = shm_td->connections;
    while (tmp != NULL) {
        if (tmp->next == shm_cd) {
        tmp->next = shm_cd->next;
        shm_cd->next = NULL;
        return;
        }
    }
    printf("Serious internal error, shm unlink_connection, connection not found\n");
    }
}
 
extern void
libcmshm_LTX_shutdown_conn(svc, shm_cd)
CMtrans_services svc;
shm_conn_data_ptr shm_cd;
{
    // if this process is the creator of the shm region, destroy it; otherwise detach it
    df_detach_shm_region (shm_cd->shm_region);
    unlink_connection(shm_cd->shm_td, shm_cd);
    free(shm_cd->filename);
    free_attr_list(shm_cd->attrs);
    free(shm_cd);
}

static int
initiate_shm_conn(cm, svc, trans, attrs, shm_conn_data, conn_attr_list)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
shm_conn_data_ptr shm_conn_data;
attr_list conn_attr_list;
{
    int int_port_num;
    shm_transport_data_ptr shm_td = (shm_transport_data_ptr) trans->trans_data;
    struct sockaddr_un dest_addr;
    char *filename;
    pid_t peer_pid;
    uint32_t num_queue_slots;
    size_t max_payload_size;

    memset(&dest_addr, 0, sizeof(dest_addr));
    if (!query_attr(attrs, CM_SHM_FILENAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & filename)) {
        svc->trace_out(cm, "CMSHM transport found no CM_SHM_FILENAME attribute");
        return -1;
    } else {
        svc->trace_out(cm, "CMSHM transport connect through file %s", filename);
    }
    if (!query_attr(attrs, CM_SHM_PID, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &peer_pid)) {
        svc->trace_out(cm, "CMSHM transport found no CM_SHM_PID attribute");
        return -1;
    } else {
        svc->trace_out(cm, "CMSHM transport connect to peer process %lu", peer_pid);
    }
    if (!query_attr(attrs, CM_SHM_NUM_SLOTS, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &num_queue_slots)) {
        svc->trace_out(cm, "CMSHM transport found no CM_SHM_NUM_SLOTS attribute");
        return -1;
    } else {
        svc->trace_out(cm, "CMSHM transport connect to peer process %lu", peer_pid);
    }
    if (!query_attr(attrs, CM_SHM_MAX_PAYLOAD, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &max_payload_size)) {
        svc->trace_out(cm, "CMSHM transport found no CM_SHM_MAX_PAYLOAD attribute");
        return -1;
    } else {
        svc->trace_out(cm, "CMSHM transport connect to peer process %lu", peer_pid);
    }

    // create a shm region and two FIFO queues in the region
    // size of each shm region is calculated according to the queue size    
    size_t queue_size = df_calculate_queue_size(num_queue_slots, max_payload_size);
    size_t region_size = 2 * queue_size;
    if(region_size % PAGE_SIZE) { 
        region_size += PAGE_SIZE - (region_size % PAGE_SIZE);
    }
    df_shm_region_t shm_region = df_create_shm_region(shm_td->shm_method, region_size, NULL); 
    if(!shm_region) {
        return -1;
    }
    
    // the shm region is laid out in memory as follows:
    // starting addr:
    // creator pid (8 byte)
    // starting offset to the starting address of send queue (8 byte)
    // starting offset to the starting address of recv queue (8 byte)
    // send queue (starting address cacheline aligned)
    // recv queue (starting address cacheline aligned)
    char *ptr = (char *) shm_region->starting_addr; // start of region, should be page-aligned
    *((uint64_t *) ptr) = (uint64_t) shm_region->creator_id;
    ptr += sizeof(uint64_t);
    char *send_q_start = (char *) shm_region->starting_addr + 2 * sizeof(uint64_t);
    if(send_q_start % CACHELINE_SIZE) {
        send_q_start += CACHELINE_SIZE - (send_q_start % CACHELINE_SIZE);
    }
    *((uint64_t *) ptr) = (uint64_t) (send_q_start - (char *) shm_region->starting_addr);
    ptr += sizeof(uint64_t);
    char *recv_q_start = send_q_start + queue_size;
    if(recv_q_start % CACHELINE_SIZE) {
        recv_q_start += CACHELINE_SIZE - (recv_q_start % CACHELINE_SIZE);
    }
    *((uint64_t *) ptr) = (uint64_t) (recv_q_start - (char *) shm_region->starting_addr);
    
    // send queue is for this process to send data to target process
    df_queue_t send_q = df_create_queue (send_q_start, num_queue_slots, max_playload_size);
    df_queue_ep_t send_ep = df_get_queue_sender_ep(send_q);
    
    // recv queue is for this process to receive data sent by target process
    df_queue_t recv_q = df_create_queue (recv_q_start, num_queue_slots, max_playload_size);
    df_queue_ep_t recv_ep = df_get_queue_receiver_ep(recv_q);    
    
    // TODO: touch each every page in shm region to warm up
    
    // get the contact info of shm region to send to target process
    int contact_len; 
    void * region_contact = df_shm_region_contact_info (shm_td->shm_method, 
        shm_conn_data->shm_region, &contact_len);
    
    // create a socket
    int sock = socket(PF_LOCAL, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror ("socket");
        exit(-1);
    }

    dest_addr.sun_family = AF_UNIX;
    strncpy (dest_addr.sun_path, filename, sizeof(dest_addr.sun_path));
    dest_addr.sun_path[sizeof (dest_addr.sun_path) - 1] = '\0';    

    // connect to the target process
    if (connect(sock, &dest_addr, addr_len) < 0) {
        perror("connect");
        exit(-1);
    }    
    
    // send the contact info of shm region to target process
    // first, send the length, then send the whole contact info as a blob
    if(sendto(sock, contact_len, sizeof(int), 0, &dest_addr, 
        sizeof(struct sockaddr_un)) == -1) {
        perror(sendto);
        exit(-1);
    }
    if(sendto(sock, region_contact, contact_len, 0, &dest_addr, 
        sizeof(struct sockaddr_un)) == -1) {
        perror(sendto);
        exit(-1);
    }
    close(sock);
    
    free(region_contact);
    shm_conn_data->shm_region = shm_region;
    shm_conn_data->send_ep = send_ep;
    shm_conn_data->recv_ep = recv_ep;
    shm_conn_data->peer_pid = peer_pid;
    shm_conn_data->filename = strdup(filename);
    shm_conn_data->dest_addr = dest_addr;
    shm_conn_data->shm_td = shm_td;
    svc->trace_out(cm, "--> Connection established");
    return 1;
}

/* 
 * Initiate a shm connection to a local process.
 * The calling process creates a shm region and creates two FIFO queues inside
 * the region, and then uses unix domain socket to send shm region info to the 
 * target process. The target process will attach this shm region.
 *
 * attrs parameter specifies the information to locate and connect target process:
 * - pid
 * - filename associated with a unix domain socket (which the target process listens to)
 */
extern CMConnection
libcmshm_LTX_initiate_conn(cm, svc, trans, attrs)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{
    shm_transport_data_ptr shm_td = trans->trans_data;
    shm_conn_data_ptr shm_conn_data = create_shm_conn_data(svc);
    attr_list conn_attr_list = create_attr_list();
    CMConnection conn;

    if (initiate_shm_conn(cm, svc, trans, attrs, shm_conn_data, conn_attr_list) != 1) {
        return NULL;
    }

    add_attr(conn_attr_list, CM_SHM_FILENAME, Attr_String,
         (attr_value) strdup(shm_conn_data->filename));
    add_attr(conn_attr_list, CM_SHM_PID, Attr_Int4,
         (attr_value) (long)shm_conn_data->peer_pid);
    add_attr(conn_attr_list, CM_SHM_NUM_SLOTS, Attr_Int4,
         (attr_value) (long)shm_conn_data->send_ep->queue->max_num_slots);
    add_attr(conn_attr_list, CM_SHM_MAX_PAYLOAD, Attr_Int4,
         (attr_value) (long)shm_conn_data->send_ep->queue->max_payload_size);
    
    conn = svc->connection_create(trans, shm_conn_data, conn_attr_list);
    add_connection(shm_conn_data->shm_td, shm_conn_data);
    shm_conn_data->conn = conn;
    shm_conn_data->attrs = conn_attr_list;
    return conn;
}

/* 
 * Check to see that if we were to attempt to initiate a connection as
 * indicated by the attribute list, would we be connecting to ourselves?
 *
 * For shm transport, if the filename (associated with the socket for connection msgs)
 * and the process pid is equal to calling process's, then it is matched.
 * The other two parameters: CM_SHM_NUM_SLOTS and CM_SHM_MAX_PAYLOAD are ignored.
 */
extern int
libcmshm_LTX_self_check(cm, svc, trans, attrs)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
{
    shm_transport_data_ptr shm_td = trans->trans_data;
    pid_t target_pid;
    char *filename = NULL;

    if (!query_attr(attrs, CM_SHM_FILENAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & filename)) {
        svc->trace_out(cm, "CMself check - SHM transport found no CM_SHM_FILENAME attribute");
        return 0;
    } 
    if (!query_attr(attrs, CM_SHM_PID, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &target_pid)) {
        svc->trace_out(cm, "CMself check - SHM transport found no CM_SHM_PID attribute");
        return 0;
    } 
    
    if (filename && (strcmp(filename, shm_td->filename) != 0)) {
        svc->trace_out(cm, "CMself check - filename doesn't match");
        return 0;
    }
    if (target_pid != shm_td->my_pid) {
        svc->trace_out(cm, "CMself check - PID doesn't match, %d, %d", my_pid, target_pid);
        return 0;
    }
    svc->trace_out(cm, "CMself check returning TRUE");
    return 1;
}

/* 
 * For shm transport, if the filename (associated with the socket for connection msgs)
 * and the process pid is equal to connection's, then it is matched.
 * The other two parameters: CM_SHM_NUM_SLOTS and CM_SHM_MAX_PAYLOAD are ignored.
 */
extern int
libcmshm_LTX_connection_eq(cm, svc, trans, attrs, shm_cd)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list attrs;
shm_conn_data_ptr shm_cd;
{
    pid_t target_pid = (pid_t) -1;
    char *filename = NULL;

    if (!query_attr(attrs, CM_SHM_FILENAME, /* type pointer */ NULL,
    /* value pointer */ (attr_value *)(long) & filename)) {
        svc->trace_out(cm, "Conn Eq  SHM transport found no CM_SHM_FILENAME attribute");
        return 0;
    } 
    if (!query_attr(attrs, CM_SHM_PID, /* type pointer */ NULL,
    /* value pointer */ (attr_value *) (long) &target_pid)) {
        svc->trace_out(cm, "Conn Eq SHM transport found no CM_SHM_PID attribute");
        return 0;
    } 
    svc->trace_out(cm, "CMShm Conn_eq comparing filename/pid %s/%d and %s/%d",
           shm_cd->filename, shm_cd->peer_pid,
           filename, target_pid);

    if (filename && (strcmp(filename, shm_cd->filename) != 0)) {
        svc->trace_out(cm, "CMShm Conn_eq filename doesn't match");
        return 0;
    }
    if (target_pid != shm_cd->peer_pid) {
        svc->trace_out(cm, "CMShm Conn_eq PID doesn't match, %d, %d", shm_cd->peer_pid, target_pid);
        return 0;
    }           
    svc->trace_out(cm, "CMShm Conn_eq returning TRUE");
    return 1;
}

/*
 * this function is called when theere is connection request on the unix 
 * domain socket. Typically the thread blocking on select() will call this
 * function. It create per-connection data and add it to the list. The listen
 * thread will poll the connection for data messages. 
 */ 
static void
libcmshm_data_available(void *vtrans, void *vinput)
{
    transport_entry trans = vtrans;
    int input_fd = (long)vinput;
    shm_transport_data_ptr shm_td = (shm_transport_data_ptr) trans->trans_data;
    shm_conn_data_ptr shm_cd = shm_td->connections;

    /* detect who is sending, and create a new 'connection' if it's new */



}

void *
listen_thread_func(void *vlsp)
{
    int timeout = 10;
    listen_struct_p lsp = vlsp;
    shm_transport_data_ptr shm_td = lsp->shm_td;
    CMtrans_services svc = lsp->svc;
    transport_entry trans = lsp->trans;
    int err;
    
    // first, wait until the listening socket is established
    while(shm_td->listen_thread_cmd == 0) { } 

    struct shm_connection_data *connections = shm_td->connections;
        
    while (shm_td->listen_thread_cmd == 1) {
        // poll for incoming data messages from shm queues
        // TODO: we need to lock the list of connections during polling
        //       since other threads may add/remove connections
        // TODO: we use a simple fair polling policy
        struct shm_connection_data *conn = connections;
        while(conn) {
            void *data = NULL;
            size_t length = 0;
            int rc = df_try_dequeue(conn->recv_ep, &data, &length); 
            switch(rc) {
                case 0: { // dequeue succeeded
                    conn->read_buffer = shm_td->svc->get_data_buffer(trans->cm, length);    
                    
                    // copy the data from shm into a cm buffer
                    memcpy(&((char*)conn->read_buffer->buffer)[0], data, length);
                    
                    // kick upstairs
                    trans->data_available(trans, conn->conn);
                    shm_td->svc->return_data_buffer(trans->cm, conn->read_buffer);
                    conn->read_buffer = NULL;
                    break;
                }
                case -1: { // no data available                 
                    break;
                }
                case 1: { // dequeue failed
                    // TODO: remedy
                    break;                
                }
                default:  
                    break;
            }
            conn = conn->next;        
        }
    }
    pthread_exit(NULL);
}

/*
 * Set up the unix domain socket to receive incoming connection messages.
 * Fork a seperate thread to handle data messages coming from shm queues.
 */
extern attr_list
libcmshm_LTX_non_blocking_listen(cm, svc, trans, listen_info)
CManager cm;
CMtrans_services svc;
transport_entry trans;
attr_list listen_info;
{
    shm_transport_data_ptr shm_td = trans->trans_data;
    attr_list listen_list;
    unsigned int nl;
    int one = 1;
    int socket_fd = -1;
    char *filename = NULL;
    struct sockaddr_un addr;
    int rc;
    listen_struct_p lsp;    
    int is_threaded; 
    
    if (listen_info == NULL || !query_attr(listen_info, CM_SHM_LISTEN_THREAD, NULL,
        (attr_value *)(long) & is_threaded)) {
        svc->trace_out(cm, "CMSHM transport found no CM_SHM_LISTEN_THREAD attribute");
        is_threaded = 1; // create listen thread by default
    }
    if(is_threaded) {
        lsp = malloc(sizeof(*lsp));
        lsp->svc = svc;
        lsp->trans = trans;
        lsp->shm_td = shm_td;
        shm_td->listen_thread_cmd = 0; // make listen thread wait
        // create a seperate thread to handle data messages from shm queue(s)
        rc = pthread_create(&(shm_td->listen_thread), NULL, shm_listen_thread_func, (void *) lsp);    
    }
    else {
        shm_td->listen_thread = 0;
    }    

    // set up a unix domain socket to receive incoming connection requests
    if (listen_info == NULL || !query_attr(listen_info, CM_SHM_FILENAME, NULL,
        (attr_value *)(long) & filename)) {
        svc->trace_out(cm, "CMSHM transport found no CM_SHM_FILENAME attribute");
        
        // try to generate a unique filename for socket
        // TODO: we need something like mkstemp to avoid race condition. for now just use
        // pid plus the timestamp value returned from gettimeofday()
        struct timeval current_time;
        filename = addr.sun_path;
        gettimeofday(&current_time, NULL);
        snprintf(filename, sizeof(addr.sun_path), "./.cmshm_sock.%ld\0", 
            (current_time.tv_sec * 1000000 + current_time.tv_usec));        
    }
    else {
        strncpy(addr.sun_path, filename, sizeof(addr.sun_path));
        addr.sun_path[sizeof(addr.sun_path)-1] = '\0';
        unlink(filename);            
    }
    unlink(filename);
    shm_td->filename = strdup(filename);
    svc->trace_out(cm, "CMSHM transport listen: use file %s", addr.sun_path);
    if ((socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(-1);
    }    
    addr.sun_family = AF_UNIX;
    int addr_len = SUN_LEN(&addr);

    // bind to filename
    if (bind(socket_fd, (struct sockaddr *) &addr, addr_len) < 0) {
        perror("bind");
        exit(-1);
    }
    
    listen_list = create_attr_list();
    add_attr(listen_list, CM_SHM_FILENAME, Attr_String,
         (attr_value) (long) strdup(filename));
    add_attr(listen_list, CM_SHM_PID, Attr_Int4,
         (attr_value) (long) shm_td->my_pid);
    add_attr(listen_list, CM_TRANSPORT, Attr_String,
         (attr_value) strdup("shm"));

    if(is_threaded == 0) {     
        svc->trace_out(cm, "CMshm Adding libcmshm_data_available as action on fd %d", socket_fd);
        svc->fd_add_select(cm, socket_fd, libcmshm_data_available,
                   (void *) trans, (void *) (long)socket_fd);
    }
    shm_td->socket_fd = socket_fd;
    if(is_threaded) {
        shm_td->listen_thread_cmd = 1; // notify listen thread to proceed
    }
    return listen_list;
}

/* 
 *  This function will not be used unless there is no read_to_buffer function
 *  in the transport.  It is an example, meant to be copied in transports 
 *  that are more efficient if they allocate their own buffer space.
 */
extern void *
libcmshm_LTX_read_block_func(svc, shm_cd, actual_len)
CMtrans_services svc;
shm_conn_data_ptr shm_cd;
int *actual_len;
{
    *actual_len = shm_cd->read_buf_len;
    shm_cd->read_buf_len = 0;
    return shm_cd->read_buffer;
}

#ifndef IOV_MAX
/* this is not defined in some places where it should be.  Conservative. */
#define IOV_MAX 16
#endif

extern int
libcmshm_LTX_writev_func(svc, shm_cd, iov, iovcnt, attrs)
CMtrans_services svc;
shm_conn_data_ptr shm_cd;
struct iovec *iov;
int iovcnt;
attr_list attrs;
{
    df_shm_queue_ep_t send_ep = shm_cd->send_ep;
    
    svc->trace_out(shm_cd->shm_td->cm, "CMShm writev of %d vectors",
           iovcnt);
           
    // put data to send queue. this is a blocking call.       
    int rc = df_enqueue_vector (send_ep, iov, iovcnt);
    if (rc != 0) {
        return -1
    }
    return iovcnt;
}

static void
free_shm_data(CManager cm, void *shm_tdv)
{
    shm_transport_data_ptr shm_td = (shm_transport_data_ptr) shm_tdv;
    CMtrans_services svc = shm_td->svc;

    close(shm_td->socket_fd);
    unlink(shm_td->filename);
    free(shm_td->filename);     
    
    // destory shm method handle and cleanup
    df_shm_finalize(shm_td->shm_method);
    svc->free_func(shm_td);
}

extern void *
libcmshm_LTX_initialize(cm, svc)
CManager cm;
CMtrans_services svc;
{
    static int atom_init = 0;

    shm_transport_data_ptr shm_data;
    svc->trace_out(cm, "Initialize CMShm transport");

    if (atom_init == 0) {
        CM_SHM_FILENAME = attr_atom_from_string("CM_SHM_FILENAME");
        CM_SHM_PID = attr_atom_from_string("CM_SHM_PID");
        CM_SHM_NUM_SLOTS = attr_atom_from_string("CM_SHM_NUM_SLOTS");
        CM_SHM_MAX_PAYLOAD = attr_atom_from_string("CM_SHM_MAX_PAYLOAD");
        CM_SHM_LISTEN_THREAD = attr_atom_from_string("CM_SHM_LISTEN_THREAD");
        CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
        atom_init++;
    }
    shm_data = svc->malloc_func(sizeof(struct shm_transport_data));
    shm_data->cm = cm;
    shm_data->svc = svc;    
    shm_data->my_pid = getpid();
    shm_data->socket_fd = -1;
    shm_data->filename = NULL;    
    shm_data->connections = NULL;

    // choose the underlying shm method to use
    // pass this choice from envrioment variable CMSHM_METHOD
    enum DF_SHM_METHOD shm_method;
    char *temp_str = cercs_getenv("CMSHM_METHOD");
    if(!temp_str || strcasecmp(temp_str, "SYSV")) {    // use system v shm by default 
        shm_method = DF_SHM_METHOD_SYSV;
    }
    else if(strcasecmp(temp_str, "MMAP")) {
        shm_method = DF_SHM_METHOD_MMAP;        
    }
    else if(strcasecmp(temp_str, "POSIXSHM")) {
        shm_method = DF_SHM_METHOD_POSIXSHM;        
    }
    else {
        fprintf(stderr, "Error: invalid CMSHM_METHOD value: %s\n"
                        "Valide options are: SYSV|MMAP|POSIXSHM\n"
                        "Use SYSV by default\n", temp_str);        
        shm_method = DF_SHM_METHOD_SYSV;
    }    
    
    // initiate underlying shm method
    shm_data->shm_method = df_shm_init(shm_method, NULL);
    if(!shm_data->shm_method) {
        svc->trace_out(cm, "Error in Initialize CMShm transport: cannot initialize shm method.");
        svc->free_func(shm_data);
        return NULL;
    }
    
    svc->add_shutdown_task(cm, free_shm_data, (void *) shm_data);
    return (void *) shm_data;
}
