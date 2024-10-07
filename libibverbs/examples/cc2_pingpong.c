/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2009-2010 Mellanox Technologies.  All rights reserved.
 * Copyright (c) 2024 NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include <config.h>

#define USE_RDMA

#define CORE_DIRECT_DEBUG
#define CALC_SUPPORT

#ifdef CORE_DIRECT_DEBUG
#define messaged(args...) printf("(%s: %d) in function %s: ",__FILE__,__LINE__,__func__); printf(args)
#else
#define messaged(args...) printf("")
#endif //CORE_DIRECT_DEBUG

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>

#include <infiniband/verbs.h>

static inline enum ibv_mtu pp_mtu_to_enum(int mtu)
{
	switch (mtu) {
	case 256:  return IBV_MTU_256;
	case 512:  return IBV_MTU_512;
	case 1024: return IBV_MTU_1024;
	case 2048: return IBV_MTU_2048;
	case 4096: return IBV_MTU_4096;
	default:   return -1;
	}
}

static inline int pp_get_local_lid(struct ibv_context *context, int port, int *lid)
{
	int ret = 0;
	struct ibv_port_attr portinfo;
	ret = ibv_query_port(context, port, &portinfo);
	if (ret) {
		fprintf(stderr, "Couldn't get port info: %d\n", ret);
		return 1;
	}

	if (portinfo.link_layer != IBV_LINK_LAYER_ETHERNET && !portinfo.lid) {
		fprintf(stderr, "Invalid link_layer: %d and local LID %d\n",
			portinfo.link_layer, portinfo.lid);
		return 1;
	}

	*lid = portinfo.lid;
	return 0;
}

#define CYCLE_BUFFER        (4096)
#define CACHE_LINE_SIZE     (64)
#define NUM_OF_RETRIES		(10)

#define KEY_GID_MSG_SIZE 	 (50+ (sizeof "00000000000000000000000000000000"))

#define BUFF_SIZE(size) ((size < CYCLE_BUFFER) ? (CYCLE_BUFFER) : (size))

typedef unsigned long int	uint64_t;

#define ALLOCATE(var,type,size)                                     \
    { if((var = (type*)malloc(sizeof(type)*(size))) == NULL)        \
        { fprintf(stderr," Cannot Allocate\n"); exit(1);}}


// Connection types availible.
#define RC  (0)
#define UC  (1)
#define UD  (2)
#define RawEth  (3)
// #define XRC 3 (TBD)

// Genral control definitions
#define OFF	     			(0)
#define ON 	     			(1)
#define SUCCESS	     		(0)
#define FAILURE	     		(1)
#define MTU_FIX	     		(7)
#define MAX_SIZE     		(8388608)
#define LINK_FAILURE 		(4)
#define MAX_OUT_READ_HERMON (16)
#define MAX_OUT_READ        (4)
#define UD_ADDITION         (40)
#define RAWETH_ADDITTION    (18)
#define HW_CRC_ADDITION    (4)
#define MAX_NODE_NUM		(8)

// Default Values of perftest parameters
#define DEF_PORT      (18515)
#define DEF_IB_PORT   (1)
#define DEF_SIZE_BW   (65536)
#define DEF_SIZE_LAT  (2)
#define DEF_ITERS     (1000)
#define DEF_ITERS_WB  (5000)
#define DEF_TX_BW     (300)
#define DEF_TX_LAT    (2000)
#define DEF_QP_TIME   (14)
#define DEF_SL	      (0)
#define DEF_GID_INDEX (-1)
#define DEF_NUM_QPS   (1)
#define DEF_INLINE_BW (0)
#define DEF_INLINE_LT (400)
#define DEF_RX_RDMA   (1)
#define DEF_RX_SEND   (600)
#define DEF_CQ_MOD    (50)
#define DEF_TOS		  (-1)
#define DEF_DURATION  (10)
#define DEF_MARGIN    (2)

// Max and Min allowed values for perftest parameters.
#define MIN_IB_PORT   (1)
#define MAX_IB_PORT   (2)
#define MIN_ITER      (5)
#define MAX_ITER      (100000000)
#define MIN_TX 	      (50)
#define MAX_TX	      (15000)
#define MIN_SL	      (0)
#define MAX_SL	      (15)
#define MIN_GID_IX    (0)
#define MAX_GID_IX    (64)
#define MIN_QP_NUM    (1)
#define MAX_QP_NUM    (8)
#define MIN_INLINE    (0)
#define MAX_INLINE    (400)
#define MIN_QP_MCAST  (1)
#define MAX_QP_MCAST  (56)
#define MIN_RX	      (1)
#define MAX_RX	      (15000)
#define MIN_CQ_MOD    (1)
#define MAX_CQ_MOD    (1000)
#define MIN_TOS 	  (0)
#define MAX_TOS		  (256)
#define RAWETH_MIN_MSG_SIZE    (64)

// The Verb of the benchmark.
typedef enum {
	SEND, WRITE, READ
} VerbType;

// The type of the machine ( server or client actually).
typedef enum {
	LAT, BW
} TestType;

// The type of the machine ( server or client actually).
typedef enum {
	SERVER, CLIENT
} MachineType;

// The type of the machine ( server or client actually).
typedef enum {
	LOCAL, REMOTE
} PrintDataSide;

// The type of the device (Hermon B0/A0 or no)
typedef enum {
	ERROR = -1, NOT_HERMON = 0, HERMON = 1
} Device;


// Type of test method.
typedef enum {
	ITERATIONS, DURATION
} TestMethod;

#define MAX(x, y) 		(((x) > (y)) ? (x) : (y))
#define MIN(x, y) 		(((x) < (y)) ? (x) : (y))
#define LAMBDA			0.00001

#define KEY_GID_PRINT_FMT "%04x:%04x:%06x:%06x:%08x:%016Lx:%s"

#if defined(CALC_SUPPORT)
struct pingpong_calc_ctx {
	enum ibv_calc_op opcode;
	enum ibv_calc_operand_type operand_type;
	enum ibv_calc_operand_size operand_size;
	uint8_t vector_count;
	void *gather_buff;
	int gather_list_size;
	struct ibv_sge *gather_list;
};
#endif /* CALC_SUPPORT */

enum {
	PP_RECV_WRID = 1, PP_SEND_WRID = 2, PP_CQE_WAIT = 3,
};

const char *wr_id_str[] = { [PP_RECV_WRID] = "RECV", [PP_SEND_WRID] = "SEND",
		[PP_CQE_WAIT] = "CQE_WAIT", };

static long page_size;

struct perftest_comm {
	struct pingpong_context *rdma_ctx;
	struct perftest_parameters *rdma_params;
	int sockfd_sd;
};

struct report_options {
	int unsorted;
	int histogram;
	int cycles; /* report delta's in cycles, not microsec's */
};

struct perftest_parameters {

	int port;
	int num_of_nodes;
	char *ib_devname;
	char *servername;
	int ib_port;
	int mtu;
	enum ibv_mtu curr_mtu;
	uint64_t size;
	int iters;
	int tx_depth;
	int qp_timeout;
	int sl;
	int gid_index;
	int all;
	int cpu_freq_f;
	int connection_type;
	int num_of_qps;
	int use_event;
	int inline_size;
	int out_reads;
	int use_mcg;
	int use_rdma_cm;
	int work_rdma_cm;
	char *user_mgid;
	int rx_depth;
	int duplex;
	int noPeak;
	int cq_mod;
	int spec;
	int tos;
	uint8_t link_type;
	MachineType machine;
	PrintDataSide side;
	VerbType verb;
	TestType tst;
	int sockfd;
	int sockfd_sd;
	int cq_size;
	float version;
	struct report_options *r_flag;
	volatile int state;
	int duration;
	int margin;
	TestMethod test_type;
	int calc_first_byte_latency;
#if defined(CALC_SUPPORT)
	/*
	 * core direct test additions:
	 */
	int verbose;
	int verify;
	enum ibv_calc_operand_type calc_data_type;
	enum ibv_calc_operand_size calc_data_size;
	// char *calc_operands_str;
	enum ibv_calc_op calc_opcode;
	uint8_t vector_count;
	int mqe_poll;
#endif /* CALC_SUPPORT */

};

struct perftest_parameters user_param;


struct pingpong_context {
	struct ibv_context *context;
	struct ibv_comp_channel *channel;
	struct ibv_pd *pd[MAX_NODE_NUM];
	struct ibv_pd *pdomain[MAX_NODE_NUM];
	struct ibv_td *td[MAX_NODE_NUM];
	struct ibv_mr *mr[MAX_NODE_NUM];
	struct ibv_cq *cq;
	struct ibv_cq *tx_cq;
	// struct ibv_cq *rx_cq[MAX_NODE_NUM];

	union {
		struct ibv_cq		*cq;
		struct ibv_cq_ex	*cq_ex;
	} rx_cq_s[MAX_NODE_NUM];

	struct ibv_qp *qp[MAX_NODE_NUM]; //in the write_lat.c code define **qp!
	struct ibv_qp_ex	*qpx[MAX_NODE_NUM];

	struct ibv_qp *mqp;
	struct ibv_cq *mcq;

	void *buf_for_calc_operands;
	void *net_buf[MAX_NODE_NUM];
	int size;
	int rx_depth;
	int pending;
	uint64_t last_result;

#if defined(CALC_SUPPORT)
	struct pingpong_calc_ctx calc_op;
#endif /* CALC_SUPPORT */
	struct ibv_ah *ah; //  add this
	int tx_depth;
	int *scnt;
	int *ccnt;
	struct rdma_event_channel *cm_channel;
	struct rdma_cm_id *cm_id_control;
	struct rdma_cm_id *cm_id;
	uint64_t *my_addr;
};

struct pingpong_dest {
	int 			   lid;
	int 			   out_reads;
	int 			   qpn;
	int 			   psn;
	unsigned           rkey;
	unsigned long long vaddr;
	union ibv_gid      gid;
	uint8_t mac[6];
};


static void my_wire_gid_to_gid(const char *wgid, union ibv_gid *gid)
{
	char tmp[9];
	__be32 v32;
	int i;
	uint32_t tmp_gid[4];

	for (tmp[8] = 0, i = 0; i < 4; ++i) {
		memcpy(tmp, wgid + i * 8, 8);
		sscanf(tmp, "%x", &v32);
		tmp_gid[i] = be32toh(v32);
	}
	memcpy(gid, tmp_gid, sizeof(*gid));
}

static void my_gid_to_wire_gid(const union ibv_gid *gid, char wgid[])
{
	uint32_t tmp_gid[4];
	int i;

	memcpy(tmp_gid, gid, sizeof(tmp_gid));
	for (i = 0; i < 4; ++i)
		sprintf(&wgid[i * 8], "%08x", htobe32(tmp_gid[i]));
}


static int pp_connect_ctx(struct pingpong_context *ctx, struct ibv_qp *qp,
		int port, int my_psn, enum ibv_mtu mtu, int sl,
		struct pingpong_dest *dest, int sgid_idx) {
	int ret;
	struct ibv_qp_attr attr = {
		.qp_state 		= IBV_QPS_RTR,
		.path_mtu 		= mtu,
		.dest_qp_num 		= dest->qpn,
		.rq_psn 		= dest->psn,
		.max_dest_rd_atomic = 1,
		.min_rnr_timer 	= 12,
		.ah_attr		= {
			.is_global	= 0,
			.dlid		= dest->lid,
			.sl		= sl,
			.src_path_bits	= 0,
			.port_num	= port
		}
	};
	if (dest->gid.global.interface_id) {
		attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = dest->gid;
		attr.ah_attr.grh.sgid_index = sgid_idx;
	}
	ret = ibv_modify_qp(qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_AV                 |
			  IBV_QP_PATH_MTU           |
			  IBV_QP_DEST_QPN           |
			  IBV_QP_RQ_PSN             |
			  IBV_QP_MAX_DEST_RD_ATOMIC |
			  IBV_QP_MIN_RNR_TIMER);
	if (ret) {
		fprintf(stderr, "%s: Failed to modify QP to RTR: %d\n", __func__, ret);
		return 1;
	}

	attr.qp_state 	    = IBV_QPS_RTS;
	attr.timeout 	    = 14;
	attr.retry_cnt 	    = 7;
	attr.rnr_retry 	    = 7;
	attr.sq_psn 	    = my_psn;
	attr.max_rd_atomic  = 1;
	ret = ibv_modify_qp(qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_TIMEOUT            |
			  IBV_QP_RETRY_CNT          |
			  IBV_QP_RNR_RETRY          |
			  IBV_QP_SQ_PSN             |
			  IBV_QP_MAX_QP_RD_ATOMIC);

	if (ret) {
		fprintf(stderr, "%s: Failed to modify QP to RTS: %d\n", __func__, ret);
		return 1;
	}

	return 0;
}

static struct pingpong_dest **pp_client_exch_dest(const char *servername,
		int port, const struct pingpong_dest *my_dest, int *sockfd_ret) {
	struct addrinfo *res, *t;
	struct addrinfo hints =
			{ .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	char *service;
	char msg[KEY_GID_MSG_SIZE];
	int n;
	int sockfd = -1;
	struct pingpong_dest **rem_dest = NULL;
	char gid[33];

	rem_dest = malloc(sizeof(struct pingpong_dest*)*1);

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(servername, service, &hints, &res);
	if (n < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
		free(service);
		return NULL;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
		return NULL;
	}

	my_gid_to_wire_gid(&my_dest->gid, gid);

	sprintf(msg, KEY_GID_PRINT_FMT,
		my_dest->lid,
		my_dest->out_reads,
		my_dest->qpn,
		my_dest->psn,
		my_dest->rkey,
		my_dest->vaddr,
		gid);
	if (write(sockfd, msg, sizeof msg) != sizeof msg) {
		fprintf(stderr, "Couldn't send local address\n");
		goto out;
	}

	if (read(sockfd, msg, sizeof msg) != sizeof msg) {
		perror("client read");
		fprintf(stderr, "Couldn't read remote address\n");
		goto out;
	}

	messaged("received server msg: %s\n", msg);

	if (write(sockfd, "done", sizeof "done") != sizeof "done") {
		fprintf(stderr, "Couldn't send done to local address\n");
		goto out;
	}

	rem_dest[0] = malloc(sizeof(struct pingpong_dest));
		if (!rem_dest[0])
			goto out;

	sscanf(msg, KEY_GID_PRINT_FMT,
		&rem_dest[0]->lid,
		&rem_dest[0]->out_reads,
		&rem_dest[0]->qpn,
		&rem_dest[0]->psn,
		&rem_dest[0]->rkey,
		&rem_dest[0]->vaddr,
		gid);
	my_wire_gid_to_gid(gid, &rem_dest[0]->gid);

	out:
//	close(sockfd);
	*sockfd_ret=sockfd;

	return rem_dest;
}

static struct pingpong_dest **pp_server_exch_dest(struct pingpong_context *ctx,
		int ib_port, enum ibv_mtu mtu, int port, int sl,
		const struct pingpong_dest my_dest[MAX_NODE_NUM], int sgid_idx,
		int num_of_nodes, int *connfd_ret) {
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		.ai_flags    = AI_PASSIVE,
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[KEY_GID_MSG_SIZE];
	int n,nodeind;
	int sockfd = -1;  //, connfd;
	struct pingpong_dest **rem_dest = NULL;
	int connfd[MAX_NODE_NUM];
	char gid[33];

	rem_dest = malloc(sizeof(struct pingpong_dest*)*num_of_nodes);

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(NULL, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
		free(service);
		return NULL;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			n = 1;
			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);
			if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't listen to port %d\n", port);
		return NULL;
	}

	for (nodeind = 0; nodeind < num_of_nodes; nodeind++) {   /////////////work?  how??
		messaged("server number  %d\n",nodeind);
		listen(sockfd, 1);
		connfd[nodeind] = accept(sockfd, NULL, 0);
	}
	close(sockfd);

	for (nodeind = 0; nodeind < num_of_nodes; nodeind++) {
		if (connfd[nodeind] < 0) {
			fprintf(stderr, "accept() failed for node %d\n", nodeind);
			return NULL;
		}
	}

	for (nodeind = 0; nodeind < num_of_nodes; nodeind++) {
		n = read(connfd[nodeind], msg, sizeof msg);
		if (n != sizeof msg) {
			perror("server read");
			fprintf(stderr, "%d/%d: Couldn't read remote address\n", n,
					(int) sizeof msg);
			goto out;
		}
		messaged("received client msg: %s\n", msg);

		rem_dest[nodeind] = malloc(sizeof(struct pingpong_dest));
		if (!rem_dest[nodeind])
			goto out;

	//	sscanf(msg, "%x:%x:%x", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn);
		sscanf(msg, KEY_GID_PRINT_FMT, &rem_dest[nodeind]->lid, &rem_dest[nodeind]->out_reads,
				&rem_dest[nodeind]->qpn,&rem_dest[nodeind]->psn, &rem_dest[nodeind]->rkey,
				&rem_dest[nodeind]->vaddr, gid);
		printf("msg reproduce: " KEY_GID_PRINT_FMT "\n",
				rem_dest[nodeind]->lid, rem_dest[nodeind]->out_reads,
				rem_dest[nodeind]->qpn, rem_dest[nodeind]->psn, rem_dest[nodeind]->rkey,
				rem_dest[nodeind]->vaddr, gid);

		my_wire_gid_to_gid(gid, &rem_dest[nodeind]->gid);

		if (pp_connect_ctx(ctx, ctx->qp[nodeind], ib_port, my_dest[nodeind].psn, mtu, sl,
				rem_dest[nodeind], sgid_idx)) {
			fprintf(stderr, "Couldn't connect to remote QP of node %d\n", nodeind);
			free(rem_dest);		///todo memory_leak
			rem_dest = NULL;
			goto out;
		}

		my_gid_to_wire_gid(&my_dest->gid, gid);

		sprintf(msg, KEY_GID_PRINT_FMT,
			my_dest[nodeind].lid,
			my_dest[nodeind].out_reads,
			my_dest[nodeind].qpn,
			my_dest[nodeind].psn,
			my_dest[nodeind].rkey,
			my_dest[nodeind].vaddr,
			gid);

		if (write(connfd[nodeind], msg, sizeof msg) != sizeof msg) {
			fprintf(stderr, "Couldn't send local address to node %d\n", nodeind);
			free(rem_dest);  ///todo memory_leak
			rem_dest = NULL;
			goto out;
		}

		n = read(connfd[nodeind], msg, sizeof msg);

		messaged("received client ack from node %d: %s\n", nodeind, msg);
	}

out:

	for (nodeind = 0; nodeind < num_of_nodes; nodeind++)
		connfd_ret[nodeind]=connfd[nodeind];

	return rem_dest;
}

static void fill_buffer(struct pingpong_context *ctx,
				enum ibv_calc_operand_type calc_data_type, enum ibv_calc_operand_size calc_data_size,
				void* net_buf, int buff_size)
{
	if (2 == calc_data_type) {
		if (calc_data_size == IBV_CALC_OPERAND_SIZE_32_BIT) {
			float *send_buf = net_buf;
			for (int i=0; i<buff_size/sizeof(send_buf[0]); i++) {
				send_buf[i] = (float)(i-1);
			}
		} else {
			double *send_buf = net_buf;
			for (int i=0; i<buff_size/sizeof(send_buf[0]); i++) {
				send_buf[i] = (double)(i-1);
			}
		}
	}
	else {
		if (calc_data_size == IBV_CALC_OPERAND_SIZE_32_BIT) {
			int32_t *send_buf = net_buf;
			for (int i=0; i<buff_size/sizeof(send_buf[0]); i++) {
				send_buf[i] = i-1;
			}
		} else {
			int64_t *send_buf = net_buf;
			for (int i=0; i<buff_size/sizeof(send_buf[0]); i++) {
				send_buf[i] = i-1;
			}
		}
	}
}

static void dump_buffer(struct pingpong_context *ctx,
				enum ibv_calc_operand_type calc_data_type, enum ibv_calc_operand_size calc_data_size,
				void* net_buf, int buff_size)
{
	if (2==calc_data_type) {
		printf("(0x%08x): ", 0);
		if (calc_data_size == IBV_CALC_OPERAND_SIZE_32_BIT) {
			int32_t *send_buf = net_buf;
			float *send_buf_float = net_buf;
			for (int i=0; i<buff_size/sizeof(send_buf[0]); i++) {
				printf("0x%08x(%f) ", send_buf[i], send_buf_float[i]);
				if ((i%8)==7) {
					printf("\n(0x%08lx): ", (i+1)*sizeof(send_buf[0]));
				}
			}
			printf("\n");
		} else {
			int64_t *send_buf = net_buf;
			double *send_buf_double = net_buf;
			for (int i=0; i<buff_size/sizeof(send_buf[0]); i++) {
				printf("0x%016lx(%lf) ", send_buf[i], send_buf_double[i]);
				if ((i%4)==3) {
					printf("\n(0x%08lx): ", (i+1)*sizeof(send_buf[0]));
				}
			}
			printf("\n");
		}
	} else {
		printf("(0x%08x): ", 0);
		if (calc_data_size == IBV_CALC_OPERAND_SIZE_32_BIT) {
			int32_t *send_buf = net_buf;
			for (int i=0; i<buff_size/sizeof(send_buf[0]); i++) {
				printf("0x%08x ", send_buf[i]);
				if ((i%8)==7) {
					printf("\n(0x%08lx): ", (i+1)*sizeof(send_buf[0]));
				}
			}
			printf("\n");
		} else {
			int64_t *send_buf = net_buf;
			for (int i=0; i<buff_size/sizeof(send_buf[0]); i++) {
				printf("0x%016lx ", send_buf[i]);
				if ((i%4)==3) {
					printf("\n(0x%08lx): ", (i+1)*sizeof(send_buf[0]));
				}
			}
			printf("\n");
		}
	}
}

#if defined(CALC_SUPPORT)

int pp_init_ctx(struct pingpong_context *ctx, struct ibv_device *ib_dev, int size,
		int tx_depth, int rx_depth, int port, int use_event,
		enum ibv_calc_op calc_op, enum ibv_calc_operand_type calc_data_type,
		enum ibv_calc_operand_size calc_data_size,
		VerbType verb);
int pp_init_ctx(struct pingpong_context *ctx, struct ibv_device *ib_dev, int size,
		int tx_depth, int rx_depth, int port, int use_event,
		enum ibv_calc_op calc_op, enum ibv_calc_operand_type calc_data_type,
		enum ibv_calc_operand_size calc_data_size,
		VerbType verb)
#else

int pp_init_ctx(struct pingpong_context *ctx, struct ibv_device *ib_dev, int size,
		int tx_depth, int rx_depth, int port, int use_event,
		VerbType verb);
int pp_init_ctx(struct pingpong_context *ctx, struct ibv_device *ib_dev, int size,
		int tx_depth, int rx_depth, int port, int use_event,
		VerbType verb)
#endif /* CALC_SUPPORT */
{

	//int rc = 0;
	int flags;
	uint64_t buff_size;

	ctx->size = size;
	ctx->rx_depth = rx_depth;

// #if defined(CALC_SUPPORT)
// 	ctx->calc_op.opcode = calc_op;
// 	ctx->calc_op.data_type = calc_data_type;
// #endif /* CALC_SUPPORT */

	buff_size = BUFF_SIZE(ctx->size) * 2 * user_param.num_of_qps;
	ctx->buf_for_calc_operands = memalign(page_size, buff_size);
	if (!ctx->buf_for_calc_operands) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		goto clean_ctx;
	}
	memset(ctx->buf_for_calc_operands, 0, size);

	ctx->net_buf[0] =memalign(page_size, buff_size);
	if (!ctx->net_buf[0]) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		goto clean_buffer;
	}
	memset(ctx->net_buf[0], 0, buff_size);


	ctx->net_buf[1] =memalign(page_size, buff_size);
	if (!ctx->net_buf[1]) {
		fprintf(stderr, "Couldn't allocate work buf 1.\n");
		goto clean_buffer;
	}
	memset(ctx->net_buf[1], 0, buff_size);

#if defined(CALC_SUPPORT)
	fill_buffer(ctx, calc_data_type, calc_data_size, ctx->net_buf[0], buff_size);

	printf("Send data (buff size: %ld, ctx->size:%d, operand_size:%d):\n",
		buff_size, ctx->size, calc_data_size);
	dump_buffer(ctx, calc_data_type, calc_data_size, ctx->net_buf[0], BUFF_SIZE(ctx->size));
#endif

	flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;

	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
				ibv_get_device_name(ib_dev));
		goto clean_net_buf;
	}

	ctx->channel = NULL;
	if (use_event) {
		ctx->channel = ibv_create_comp_channel(ctx->context);
		if (!ctx->channel) {
			fprintf(stderr, "Couldn't create completion channel\n");
			goto clean_device;
		}
	} else
		ctx->channel = NULL;

	ctx->pd[0] = ibv_alloc_pd(ctx->context);
	if (!ctx->pd[0]) {
		fprintf(stderr, "Couldn't allocate PD\n");
		goto clean_comp_channel;
	}

	if (verb == READ){
		flags |= IBV_ACCESS_REMOTE_READ;
	}


	ctx->mr[0] = ibv_reg_mr(ctx->pd[0], ctx->net_buf[0], buff_size, flags);  ///sasha,  was  size
	if (!ctx->mr[0]) {
		fprintf(stderr, "Couldn't register MR\n");
		goto clean_pd;
	}

	ctx->mr[1] = ibv_reg_mr(ctx->pd[0], ctx->net_buf[1], buff_size, flags);  ///sasha,  was  size
	if (!ctx->mr[1]) {
		fprintf(stderr, "Couldn't register MR 1\n");
		goto clean_pd;
	}

	///////////////////////////////////////////////////////////////////////////////////////
	ctx->cq = ibv_create_cq(ctx->context, rx_depth + 1, NULL, ctx->channel, 0);  //sasha  was rx_depth
	if (!ctx->cq) {
		fprintf(stderr, "Couldn't create CQ (%s:%d)\n", __FUNCTION__, __LINE__);
		goto clean_mr;
	}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	{
		struct ibv_qp_init_attr init_attr = {
			.send_cq = ctx->cq,
			.recv_cq = ctx->cq,
			.cap     = {
				.max_send_wr  = tx_depth,
				.max_recv_wr  = rx_depth,
				.max_send_sge = 1,
				.max_recv_sge = 1,
				// .max_inline_data = 8,
			},
			.qp_type = IBV_QPT_RC
		};
		ctx->qp[0] = ibv_create_qp(ctx->pd[0], &init_attr);
		if (!ctx->qp[0]) {
			fprintf(stderr, "Couldn't create QP\n");
			goto clean_cq;
		}
	}

	{
		struct ibv_qp_attr attr = {
				.qp_state = IBV_QPS_INIT,
				.pkey_index = 0,
				.port_num = port,
				.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE
		};

		if (ibv_modify_qp(ctx->qp[0], &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify QP to INIT\n");
			goto clean_qp;
		}
	}

	return 0;

	/* clean_mqp: */ ibv_destroy_qp(ctx->mqp);

	/* clean_mcq: */ ibv_destroy_cq(ctx->mcq);

	clean_qp: ibv_destroy_qp(ctx->qp[0]);

	clean_cq: ibv_destroy_cq(ctx->cq);

	clean_mr: ibv_dereg_mr(ctx->mr[0]);

	clean_pd: ibv_dealloc_pd(ctx->pd[0]);

	clean_comp_channel:
		if (ctx->channel) {
			ibv_destroy_comp_channel(ctx->channel);
		}

	clean_device: ibv_close_device(ctx->context);

	clean_net_buf: free(ctx->net_buf[0]);

	clean_buffer: free(ctx->buf_for_calc_operands);

	clean_ctx: free(ctx);

	return 0;
}
//////////////////////////////////////////////////

#if defined(CALC_SUPPORT)

int pp_init_ctx_server(struct pingpong_context *ctx, struct ibv_device *ib_dev, int size,
		int tx_depth, int rx_depth, int port, int use_event,
		enum ibv_calc_op calc_op, enum ibv_calc_operand_type calc_data_type,
		enum ibv_calc_operand_size calc_data_size, uint8_t vector_count,
		// char *calc_operands_str,
		VerbType verb);
int pp_init_ctx_server(struct pingpong_context *ctx, struct ibv_device *ib_dev, int size,
		int tx_depth, int rx_depth, int port, int use_event,
		enum ibv_calc_op calc_op, enum ibv_calc_operand_type calc_data_type,
		enum ibv_calc_operand_size calc_data_size, uint8_t vector_count,
		// char *calc_operands_str,
		VerbType verb)
#else

int pp_init_ctx_server(struct pingpong_context *ctx, struct ibv_device *ib_dev, int size,
		int tx_depth, int rx_depth, int port, int use_event,
		VerbType verb);
int pp_init_ctx_server(struct pingpong_context *ctx, struct ibv_device *ib_dev, int size,
		int tx_depth, int rx_depth, int port, int use_event,
		VerbType verb)
#endif /* CALC_SUPPORT */
{

	int i, ret;
	int flags;
	uint64_t buff_size;

	ctx->size = size;
	ctx->rx_depth = rx_depth;

#if defined(CALC_SUPPORT)
	ctx->calc_op.opcode = calc_op;
	ctx->calc_op.operand_type = calc_data_type;
	ctx->calc_op.operand_size = calc_data_size;
	ctx->calc_op.vector_count = vector_count;
#endif /* CALC_SUPPORT */

	buff_size = BUFF_SIZE(ctx->size) * 2 * user_param.num_of_qps;
	ctx->buf_for_calc_operands = memalign(page_size, buff_size);
	if (!ctx->buf_for_calc_operands) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		goto clean_ctx;
	}
	memset(ctx->buf_for_calc_operands, 0, size);

	for(i=0; i<user_param.num_of_nodes; i++)
	{
		ctx->net_buf[i] =memalign(page_size, buff_size);
		if (!ctx->net_buf[i]) {
			fprintf(stderr, "Couldn't allocate work buf.\n");
			goto clean_buffer;
		}
		memset(ctx->net_buf[i], 0, buff_size);
	}

	flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;

	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
				ibv_get_device_name(ib_dev));
		goto clean_net_buf;
	}

	if (use_event) {
		ctx->channel = ibv_create_comp_channel(ctx->context);
		if (!ctx->channel) {
			fprintf(stderr, "Couldn't create completion channel\n");
			goto clean_device;
		}
	} else
		ctx->channel = NULL;

	// for(i=0; i<user_param.num_of_nodes; i++)
	for(i=0; i<1; i++)
	{
		ctx->pd[i] = ibv_alloc_pd(ctx->context);
		if (!ctx->pd[i]) {
			fprintf(stderr, "Couldn't allocate PD[%d]\n", i);
			goto clean_comp_channel;
		}
		struct ibv_td_init_attr td_init_attr = {
			.comp_mask = 0,
		};
		ctx->td[i] = ibv_alloc_td(ctx->context, &td_init_attr);
		if (!ctx->td[i]) {
			fprintf(stderr, "Couldn't allocate TD\n");
			goto clean_pd1;
		}

		struct ibv_parent_domain_init_attr pdomain_init_attr = {
			.pd = ctx->pd[i],
			.td = ctx->td[i],
			.comp_mask = 0,
		};
		ctx->pdomain[i] = ibv_alloc_parent_domain(ctx->context, &pdomain_init_attr);
		if (!ctx->pdomain[i]) {
			fprintf(stderr, "Couldn't allocate TD\n");
			goto clean_pd2;
		}
	}

	if (verb == READ){
		flags |= IBV_ACCESS_REMOTE_READ;
	}


	for(i=0; i<user_param.num_of_nodes; i++)
	{
		// ctx->mr[i] = ibv_reg_mr(ctx->pd[i], ctx->net_buf[i], buff_size, flags);
		ctx->mr[i] = ibv_reg_mr(ctx->pd[0], ctx->net_buf[i], buff_size, flags);
		if (!ctx->mr[i]) {
			fprintf(stderr, "Couldn't register MR %d\n", i);
			goto clean_pd;
		}
	}

	printf("ibv_create_cq with tx_depth=%d\n",tx_depth);
	ctx->tx_cq = ibv_create_cq(ctx->context, tx_depth + 1, NULL, ctx->channel, 0);  // Question? the size of rx_depth
	if (!ctx->tx_cq) {
		fprintf(stderr, "Couldn't create tx CQ\n");
		goto clean_mr;
	}
	{
		struct ibv_qp_init_attr_ex init_attr_ex = {
			.send_cq = ctx->tx_cq,
			// .recv_cq = ctx->rx_cq,
			.cap     = {
				.max_send_wr  = tx_depth,
				.max_recv_wr  = rx_depth,
				.max_send_sge = 1,
				.max_recv_sge = 1,
				// .max_inline_data = 8,
			},
			.qp_type = IBV_QPT_RC,
			.comp_mask = IBV_QP_INIT_ATTR_PD | IBV_QP_INIT_ATTR_CREATE_FLAGS | IBV_QP_INIT_ATTR_SEND_OPS_FLAGS,
			.create_flags = IBV_QP_CREATE_CROSS_CHANNEL,
			.send_ops_flags = IBV_QP_EX_WITH_RDMA_WRITE | IBV_QP_EX_WITH_RDMA_WRITE_WITH_IMM
					| IBV_QP_EX_WITH_SEND | IBV_QP_EX_WITH_SEND_WITH_IMM
					|IBV_QP_EX_WITH_VECTOR_CALC,
		};

		for(i=0; i<user_param.num_of_nodes; i++)
		{

			printf("ibv_create_cq rx_cq[%d] with rx_depth=%d\n",i, rx_depth);
			struct ibv_cq_init_attr_ex attr_ex = {
				.cqe = rx_depth + 1,
				.cq_context = NULL,
				.channel = ctx->channel,
				.comp_vector = 0,
				.parent_domain = ctx->pdomain[0],
				.flags = IBV_CREATE_CQ_ATTR_IGNORE_OVERRUN,
				.comp_mask = IBV_CQ_INIT_ATTR_MASK_PD | IBV_CQ_INIT_ATTR_MASK_FLAGS
				// .wc_flags = IBV_WC_EX_WITH_COMPLETION_TIMESTAMP
			};

			ctx->rx_cq_s[i].cq_ex = ibv_create_cq_ex(ctx->context, &attr_ex);
			if (!ctx->rx_cq_s[i].cq_ex) {
					fprintf(stderr, "Couldn't create rx CQ[%d]\n", i);
					goto clean_mr;
			}

			init_attr_ex.recv_cq = ibv_cq_ex_to_cq(ctx->rx_cq_s[i].cq_ex);//ctx->rx_cq[i];

			init_attr_ex.pd = ctx->pd[0];
			ctx->qp[i] = ibv_create_qp_ex(ctx->context, &init_attr_ex);
			ctx->qpx[i] = ibv_qp_to_qp_ex(ctx->qp[i]);
			if (!ctx->qp[i]) {
				fprintf(stderr, "Couldn't create QP for node %d\n",i);
				goto clean_cq;
			} else {
				printf("Create QP[%d] returns qp_num %d\n", i, ctx->qp[i]->qp_num);
			}
		}
	}

	{
		struct ibv_qp_attr attr = {
			.qp_state = IBV_QPS_INIT,
			.pkey_index = 0,
			.port_num = port,
			.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE
		}; /*sasha  was 0 */

		for(i=0; i<user_param.num_of_nodes; i++) {
			ret = ibv_modify_qp(ctx->qp[i], &attr,
					IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT
						| IBV_QP_ACCESS_FLAGS);
			if (ret) {

				fprintf(stderr, "Failed to modify QP number %d to INIT, ret %d\n",i, ret);
				goto clean_qp;
			}
		}
	}

	return 0;

#if defined(CALC_SUPPORT)
	clean_mqp: ibv_destroy_qp(ctx->mqp);

	clean_mcq: ibv_destroy_cq(ctx->mcq);

	clean_qp: ibv_destroy_qp(ctx->qp);

	clean_cq:
		if (ctx->cq)
			ibv_destroy_cq(ctx->cq);

	clean_mr: ibv_dereg_mr(ctx->mr);

	clean_pd2:

	clean_pd1:

	clean_pd: ibv_dealloc_pd(ctx->pd);

	clean_comp_channel: if (ctx->channel)
		ibv_destroy_comp_channel(ctx->channel);

	clean_device: ibv_close_device(ctx->context);

	clean_net_buf: free(ctx->net_buf);

	clean_buffer: free(ctx->buf_for_calc_operands);

	clean_ctx: free(ctx);
#else
	clean_qp:
	clean_cq:

	clean_mr:

	clean_pd2:

	clean_pd1:

	clean_pd:

	clean_comp_channel: if (ctx->channel)
		ibv_destroy_comp_channel(ctx->channel);

	clean_device: ibv_close_device(ctx->context);

	clean_net_buf:
	for(i=0; i<user_param.num_of_nodes; i++) {
		if (ctx->net_buf[i])
			free(ctx->net_buf[i]);
	}

	clean_buffer: free(ctx->buf_for_calc_operands);

	clean_ctx: free(ctx);
#endif /* CALC_SUPPORT */

	return 0;
}



int pp_close_ctx(struct pingpong_context *ctx);
int pp_close_ctx(struct pingpong_context *ctx) {
	int i;


	for(i=0; i<user_param.num_of_nodes; i++)
		if (ctx->qp[i] && ibv_destroy_qp(ctx->qp[i])) {
			fprintf(stderr, "Couldn't destroy QP %d\n", i);
			return 1;
		}

	if (ctx->cq && ibv_destroy_cq(ctx->cq)) {
		fprintf(stderr, "Couldn't destroy CQ (%s:%d)\n", __FUNCTION__, __LINE__);
		return 1;
	}
	ctx->cq = 0;

	if (ctx->tx_cq && ibv_destroy_cq(ctx->tx_cq)) {
		fprintf(stderr, "Couldn't destroy tx CQ  (%s:%d)\n",  __FUNCTION__, __LINE__);
		return 1;
	}
	for(i=0; i<user_param.num_of_nodes; i++)
		if (ctx->rx_cq_s[i].cq && ibv_destroy_cq(ctx->rx_cq_s[i].cq)) {
			fprintf(stderr, "Couldn't destroy rx CQ %d (%s:%d)\n", i, __FUNCTION__, __LINE__);
			return 1;
		}

	for(i=0; i<user_param.num_of_nodes; i++)
		if (ctx->mr[i] && ibv_dereg_mr(ctx->mr[i])) {
			fprintf(stderr, "Couldn't deregister MR\n");
			return 1;
		}

	// for(i=0; i<user_param.num_of_nodes; i++)
	for(i=0; i<1; i++)
		if (ctx->pd[i] && ibv_dealloc_pd(ctx->pd[i])) {
			fprintf(stderr, "Couldn't deallocate PD\n");
			return 1;
		}

	if (ctx->channel) {
		if (ibv_destroy_comp_channel(ctx->channel)) {
			fprintf(stderr, "Couldn't destroy completion channel\n");
			return 1;
		}
	}

	if (ibv_close_device(ctx->context)) {
		fprintf(stderr, "Couldn't release context\n");
		return 1;
	}

	free(ctx->buf_for_calc_operands);
	free(ctx->net_buf[0]);
	free(ctx);

	return 0;
}

static int pp_post_recv(struct pingpong_context *ctx, int n) {

	int sum=0;

	if (user_param.servername)		//client
	{

	struct ibv_sge list = { .addr = (uintptr_t) ctx->net_buf[1], .length =
			ctx->size, .lkey = ctx->mr[1]->lkey };
	struct ibv_recv_wr wr = { .wr_id = PP_RECV_WRID, .sg_list = &list,
			.num_sge = 1, .next = NULL, };
	struct ibv_recv_wr *bad_wr;
	int i;

	for (i = 0; i < n; ++i)
		if (ibv_post_recv(ctx->qp[0], &wr, &bad_wr))
			break;

	return i;
	}

	else		//server
	{
		int j;
		for (j=0; j<user_param.num_of_nodes; j++)
		{
			struct ibv_sge list = { .addr = (uintptr_t) ctx->net_buf[j], .length =
						ctx->size, .lkey = ctx->mr[j]->lkey };
				struct ibv_recv_wr wr = { .wr_id = PP_RECV_WRID, .sg_list = &list,
						.num_sge = 1, .next = NULL, };
				struct ibv_recv_wr *bad_wr;
				int i;

				for (i = 0; i < n; ++i)
					if (ibv_post_recv(ctx->qp[j], &wr, &bad_wr))
						break;

				sum=sum+i;
		}
		return sum;
	}

}

/*
 * pp_post_send:
 * post SEND request on the QP
 *
 */
static int pp_post_send(struct pingpong_context *ctx, struct pingpong_dest *rem_dest) {
	int ret;

	struct ibv_sge list = {
		.addr = (uintptr_t) ctx->net_buf[0],
		.length = ctx->size,
		.lkey = ctx->mr[0]->lkey
	};

	struct ibv_send_wr *bad_wr;
	struct ibv_send_wr wr =
	{
		.wr_id = PP_SEND_WRID,
		.sg_list = &list,
		.num_sge = 1,
	#if defined(USE_RDMA)
		.opcode = IBV_WR_RDMA_WRITE_WITH_IMM,
		.wr =
		{
			.rdma =
			{
				.remote_addr = rem_dest->vaddr,
				.rkey = rem_dest->rkey //.send_flags = IBV_SEND_SIGNALED, //requst fo cqe
			}
		}
	#else
		.opcode = IBV_WR_SEND
	#endif  // USE_RDMA
		,
	};

	ret = ibv_post_send(ctx->qp[0], &wr, &bad_wr);
	if (ret) {
		messaged("error in ibv_post_send\n");
	}

	return ret;

}


int server_pre_post_wqes(struct pingpong_context *ctx, int iters,struct pingpong_dest **rem_dest, int num_of_nodes);
int server_pre_post_wqes(struct pingpong_context *ctx, int iters,struct pingpong_dest **rem_dest, int num_of_nodes) {
	int ret = 0;
	int i;
	struct ibv_send_wr *bad_wr;
	// struct ibv_send_wr wr;
	// struct ibv_sge     		list;
	int j;

	for (i = 0; i < iters; i++) {
		for (j=0; j<num_of_nodes; j++){
			// let node j wait data from next node j+1
			struct ibv_send_wr wr = {
				.next = NULL,
				.wr_id	    = PP_CQE_WAIT,
				.sg_list    = NULL,
				.num_sge    = 0,
				.opcode     = IBV_WR_CQE_WAIT,
				.send_flags = 0,
				.wr = {
					.cqe_wait = {
						.cq = ctx->rx_cq_s[(j+1)%num_of_nodes].cq, // wait on next nodes's message
						.cq_count = 1, //num_of_nodes,
					}
				}
			};
			if (i == (iters - 1))
				wr.send_flags |= IBV_SEND_SIGNALED;
			else {
				wr.send_flags &= ~IBV_SEND_SIGNALED;
			}

			// if (j == (num_of_nodes - 1))
				wr.send_flags |= IBV_SEND_WAIT_EN_LAST;

			ret = ibv_post_send(ctx->qp[j], &wr, &bad_wr);

			if (ret) {
				fprintf(stderr, "-E- ibv_post_send verb wait_cqe failed \n");
				return -1;
			}
		}

		for (j=0; j<num_of_nodes; j++){
			// send to node j with data received from next node j+1
			struct ibv_sge list = {
				.addr	= (uintptr_t)ctx->net_buf[(j+1)%num_of_nodes],
				.length = user_param.size,
				.lkey	= ctx->mr[(j+1)%num_of_nodes]->lkey
			};

			struct ibv_send_wr wr = {
				.next = NULL,
				.wr_id	    = PP_SEND_WRID,
				.sg_list    = &list,
				.num_sge    = 1,
#if defined(USE_RDMA)
				.opcode = IBV_WR_RDMA_WRITE_WITH_IMM,
				.wr = {
					.rdma = {
						.remote_addr = rem_dest[j]->vaddr,
						.rkey = rem_dest[j]->rkey,
					}
				}
#else
				.opcode = IBV_WR_SEND,
#endif  /* USE_RDMA */
			};

			if (i == (iters - 1)) {
				wr.send_flags |= IBV_SEND_SIGNALED;
			}
			else {
				wr.send_flags &= ~IBV_SEND_SIGNALED;
			}

#if defined(CALC_SUPPORT)
		if (ctx->calc_op.opcode != IBV_CALC_OP_NA) {
			// op 0,1,2,3,4,5,6,7 are verified
			wr.vector_calc.op = ctx->calc_op.opcode;

			// 0,1 are verified with max and min op
			// since max/min(0xffffffff,0x1ff) is different in signed an unsigned int cases.
			// 2 -float/double os verified
			wr.vector_calc.operand_type = ctx->calc_op.operand_type;

			// 4 or 8 bytes are verified
			wr.vector_calc.operand_size = ctx->calc_op.operand_size;

			wr.vector_calc.tag_type = 0;
			wr.vector_calc.tag_size = 0;
			wr.vector_calc.tag_exist = 0;

			// little_endian verified: switch to 0 makes float/double add op ends up with wrong result
			wr.vector_calc.little_endian = 1;

			// don't know how to verify it yet
			wr.vector_calc.chunk_size = 0;

			// verified by setting to 1, 2 or 4
			wr.vector_calc.vector_count = ctx->calc_op.vector_count;

			list.length = list.length/wr.vector_calc.vector_count;

			wr.send_flags |= IBV_SEND_VECTOR_CALC;
		}
#endif // CALC_SUPPORT

			ret = ibv_post_send(ctx->qp[j], &wr, &bad_wr);
			if (ret) {
				fprintf(stderr, "-E- ibv_post_send verb send failed \n");
				return -1;
			}
		}
	}

	return ret;
}

static void usage(const char *argv0) {
	printf("Usage:\n");
	printf("  %s				start a server and wait for connection\n", argv0);
	printf("  %s <host>	 		connect to server at <host>\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf(
			"  -p, --port=<port>		listen on/connect to port <port> (default 18515)\n");
	printf(
			"  -d, --ib-dev=<dev>		use IB device <dev> (default first device found)\n");
	printf(
			"  -i, --ib-port=<port>		use port <port> of IB device (default 1)\n");
	printf("  -s, --size=<size>		size of message to exchange (default 4096)\n");
	printf("  -m, --mtu=<size>		path MTU (default 1024)\n");
	printf(
			"  -r, --rx-depth=<dep>		number of receives to post at a time (default 500)\n");
	printf("  -n, --iters=<iters>		number of exchanges (default 1000)\n");
	printf("  -l, --sl=<sl>			service level value\n");
	printf("  -e, --events			sleep on CQ events (default poll)\n");
#if defined(CALC_SUPPORT)
	printf("  -c, --calc=<operation>	calc operation, 0-disabled, 1-add, 2-max, 3-and,4-or,5-xor, 6-min, 7-swap endian\n");
	printf("  -t, --op_type=<type>		calc operands type, 0-uint32, 1-int32, 2-float\n");
	printf("  -w, --op_size=<size>      calc operands size, 0-4Bytes, 1-8Bytes");
	printf("  -v, --verbose			print verbose information\n");
#endif /* CALC_SUPPORT */
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///----------------------------------sassha additions--------------------------------------------////

static void init_perftest_params(struct perftest_parameters *user_param1) { ///todo set client and server!! sashas

	user_param1->port = DEF_PORT;
	user_param1->ib_port = DEF_IB_PORT;
	user_param1->size = user_param1->tst == BW ? DEF_SIZE_BW : DEF_SIZE_LAT;
	user_param1->tx_depth = user_param1->tst == BW ? DEF_TX_BW : DEF_TX_LAT;
	user_param1->qp_timeout = DEF_QP_TIME;
	user_param1->all = OFF;
	user_param1->cpu_freq_f = OFF;
	user_param1->connection_type = RC;
	user_param1->use_event = OFF;
	user_param1->num_of_qps = DEF_NUM_QPS;
	user_param1->gid_index = DEF_GID_INDEX;
	user_param1->inline_size =
	user_param1->tst == BW ? DEF_INLINE_BW : DEF_INLINE_LT;
	user_param1->use_mcg = OFF;
	user_param1->use_rdma_cm = ON;  ////sasha was OFF
	user_param1->work_rdma_cm = OFF;
	user_param1->rx_depth = user_param1->verb == SEND ? DEF_RX_SEND : DEF_RX_RDMA;
	user_param1->duplex = OFF;
	user_param1->noPeak = OFF;
	user_param1->cq_mod = DEF_CQ_MOD;
	user_param1->tos = DEF_TOS;

	user_param1->test_type = ITERATIONS;
	user_param1->duration = DEF_DURATION;
	user_param1->margin = DEF_MARGIN;

	user_param1->iters =
			(user_param1->tst == BW && user_param1->verb == WRITE) ?
					DEF_ITERS_WB : DEF_ITERS;
	user_param1->calc_first_byte_latency = OFF;
#if defined(CALC_SUPPORT)

	/*
	 * core direct additions:
	 */
	user_param1->verbose = 0;
	user_param1->verify = 0;
	user_param1->calc_data_type = 0;
	user_param1->calc_data_size = 0;
	// user_param1->calc_operands_str = NULL;
	user_param1->calc_opcode = IBV_CALC_OP_NA;
	user_param1->mqe_poll = 0;
#endif /* CALC_SUPPORT */

	if (user_param1->tst == LAT) {
		user_param1->r_flag->unsorted = OFF;
		user_param1->r_flag->histogram = OFF;
		user_param1->r_flag->cycles = OFF;
	}

}

/******************************************************************************
 *
 ******************************************************************************/



int check_add_port(char **service, int port, const char *servername,
		struct addrinfo *hints, struct addrinfo **res);
int check_add_port(char **service, int port, const char *servername,
		struct addrinfo *hints, struct addrinfo **res) {

	int number;
	if (asprintf(service, "%d", port) < 0)
		return FAILURE;
	number = getaddrinfo(servername, *service, hints, res);

	if (number < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(number), servername,
				port);
		return FAILURE;
	}
	return SUCCESS;
}

int parser(struct perftest_parameters *user_param1, int argc, char *argv[]);
/*
 * parser: get all user parameters and assign to the user_param global struct
 */
int parser(struct perftest_parameters *user_param1, int argc, char *argv[]) {

	int port = 18515;
	int ib_port = 1;
	int size = 4096;
	int gid_index = -1;

	enum ibv_mtu mtu = IBV_MTU_1024;
	int rx_depth = 8000; /*oferh: was 500*/
	//int tx_depth = 16000;
	int iters = DEF_ITERS;
	int use_event = 0;
	//int rcnt, scnt;
	int sl = 0;
	int mqe_poll = 0;
	int verbose = 0;
	int verify = 0;

	char *ib_devname = NULL;

#if defined(CALC_SUPPORT)
	enum ibv_calc_operand_type calc_data_type = 0;
	enum ibv_calc_op		   calc_opcode      = IBV_CALC_OP_NA;
	enum ibv_calc_operand_size calc_data_size = 0;
	int vector_count=2;
	// char *calc_operands_str = NULL;
#endif /* CALC_SUPPORT */


	srand48(getpid() * time(NULL));

	page_size = sysconf(_SC_PAGESIZE);

	init_perftest_params(user_param1);

	while (1) {
		int c;

		static struct option long_options[] = {
				{ .name = "port",     .has_arg = 1, .val = 'p' },
				{ .name = "ib-dev",   .has_arg = 1, .val = 'd' },
				{ .name = "ib-port",  .has_arg = 1, .val = 'i' },
				{ .name = "size",     .has_arg = 1, .val = 's' },
				{ .name = "mtu",      .has_arg = 1, .val = 'm' },
				{ .name = "rx-depth", .has_arg = 1, .val = 'r' },
				{ .name = "iters",    .has_arg = 1, .val = 'n' },
				{ .name = "sl",       .has_arg = 1, .val = 'l' },
				{ .name = "events",   .has_arg = 0, .val = 'e' },
				{ .name = "gid-idx",  .has_arg = 1, .val = 'g' },
				{ .name = "calc_op",  .has_arg = 1, .val = 'c' },
				{ .name = "op_type",  .has_arg = 1, .val = 't' },
				{ .name = "op_size",  .has_arg = 1, .val = 'w' },
				{ .name = "vector_count", .has_arg = 1, .val = 'o' },
				// { .name = "poll_mqe", .has_arg = 0, .val = 'w' },
				{ .name = "verbose",  .has_arg = 0, .val = 'v' },
				{ .name = "verify",   .has_arg = 0, .val = 'V' },
				{ 0 } };

		// c = getopt_long(argc, argv, "p:d:i:s:m:r:n:l:et:g:c:o:wfvV", long_options, NULL);
		c = getopt_long(argc, argv, "p:d:i:s:m:r:n:l:et:g:c:w:o:vV", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'p':
			port = strtol(optarg, NULL, 0);
			if (port < 0 || port > 65535) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'd':
			ib_devname = (char *) malloc(
					sizeof(char) * strlen(strdupa(optarg)) + 1);
			strcpy(ib_devname, strdupa(optarg));
			break;

		case 'i':
			ib_port = strtol(optarg, NULL, 0);
			if (ib_port < 0) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 's':
			size = strtol(optarg, NULL, 0);
			break;

		case 'm':
			mtu = pp_mtu_to_enum(strtol(optarg, NULL, 0));
			if (mtu < 0) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'r':
			rx_depth = strtol(optarg, NULL, 0);
			break;

		case 'n':
			iters = strtol(optarg, NULL, 0);

			if (iters * 2 > rx_depth) {
				fprintf(stderr, "iters*2 > rx_depth");
				return -1;
			}
			break;

		case 'l':
			sl = strtol(optarg, NULL, 0);
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			verify = 1;
			break;

		case 'e':
			++use_event;
			break;

		case 'g':
			gid_index = strtol(optarg, NULL, 0);
			break;

#if defined(CALC_SUPPORT)
		case 't':
			calc_data_type = strtol(optarg, NULL, 0);
			if (calc_data_type > IBV_CALC_OPERAND_TYPE_FLOAT) {
				printf("-E- invalid data type. Valid values are: 0-1\n");
				return 1;
			}
			break;

		case 'w':
			calc_data_size = strtol(optarg, NULL, 0);
			if (calc_data_size > IBV_CALC_OPERAND_SIZE_64_BIT) {
				printf("-E- invalid data size. Valid values are: 0-1\n");
				return 1;
			}
			break;

		case 'o':
			vector_count = strtol(optarg, NULL, 0);
			break;
		case 'c':
			// calc_opcode = ibv_m_str_to_calc_op(optarg);
			calc_opcode = strtol(optarg, NULL, 0);
			if (calc_opcode > IBV_CALC_OP_SWAP_ENDIAN) {
				printf("-E- invalid data types. Valid values are: 1 - 7\n");
				return 1;
			}
			break;
#endif /* CALC_SUPPORT */

		default:
			usage(argv[0]);
			return 1;
		}
	}

	/*
	 * assign to user_param:
	 */
	user_param1->port = port;
	user_param1->ib_devname = ib_devname;
	user_param1->ib_port = ib_port;
	user_param1->size = size;
	user_param1->mtu = mtu;
	user_param1->rx_depth = rx_depth;
	user_param1->iters = iters;
	user_param1->tx_depth=2*iters;
	user_param1->sl = sl;
	user_param1->use_event = use_event;
#if defined(CALC_SUPPORT)
	user_param1->verbose = verbose;
	user_param1->verify = verify;
	user_param1->calc_data_type = calc_data_type;
	user_param1->calc_data_size = calc_data_size;
	// user_param1->calc_operands_str = calc_operands_str;
	user_param1->calc_opcode = calc_opcode;
	user_param1->vector_count = vector_count;
	user_param1->mqe_poll = mqe_poll;
#endif /* CALC_SUPPORT */
	user_param1->gid_index = gid_index;

	return SUCCESS;
}



/******************************************************************************
 *
 ******************************************************************************/

int main(int argc, char *argv[]) {
	struct ibv_device **dev_list;
	struct ibv_device *ib_dev = NULL;
	struct pingpong_context *ctx;
	struct pingpong_dest my_dest[MAX_NODE_NUM];
	struct pingpong_dest **rem_dest;
	struct timeval start, end;

	int routs;
	int num_cq_events = 0;
	int comp_count;

	char msg[KEY_GID_MSG_SIZE];
	int connfd[MAX_NODE_NUM];
	int sockfd = 0;

	struct ibv_wc wc[2];
	int ne, i;

	int ret;
	union ibv_gid temp_gid;
	int j;

	struct report_options report = { };
	// struct perftest_comm user_comm; ///todo
	char *servername = NULL;

	ctx = malloc(sizeof *ctx);		///sasha  was in pp_init_ctx!!
	if (!ctx)
		return 0;
	memset(ctx, 0, sizeof *ctx);

	/* init default values to user's parameters */
	memset(&user_param, 0, sizeof(struct perftest_parameters)); //sasha, do I need this?? define global in write.lat
	// memset(&user_comm, 0, sizeof(struct perftest_comm));
	memset(&my_dest, 0, sizeof(struct pingpong_dest));

	user_param.verb = WRITE;
	user_param.tst = LAT;
	user_param.r_flag = &report;
	user_param.version = 0; // VERSION;
	user_param.num_of_nodes= 2; //todo

	/*
	 * parse input arguments:
	 */
	if (parser(&user_param, argc, argv)) {
		fprintf(stderr, " Parser function exited with Error\n");
		return FAILURE;
	}

	if (optind == argc - 1) //sasha
		user_param.machine = CLIENT;
	else
		user_param.machine = SERVER;

	if (optind == argc - 1) {
		servername = strdupa(argv[optind]);
	} else if (optind < argc) {
		usage(argv[0]);
		return 1;
	}
	user_param.servername = servername;

	/*
	 * connect to the IB device:
	 */
	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		fprintf(stderr, "No IB devices found\n");
		return 1;
	}

	if (user_param.ib_devname) {
		// int i;

		for (i = 0; dev_list[i]; ++i) {
			if (!strcmp(ibv_get_device_name(dev_list[i]),
					user_param.ib_devname)) {
				ib_dev = dev_list[i];
				break;
			}
		}
		if (!ib_dev) {
			fprintf(stderr, "IB device %s not found\n", user_param.ib_devname);
			return 1;
		}
	} else
		ib_dev = *dev_list;

#if defined(CALC_SUPPORT)

		// user_param.calc_operands_str="1,2";  // TODO  delite this.   /

		if (user_param.servername)		/*client*/
		{
			ret = pp_init_ctx(ctx,ib_dev, user_param.size, user_param.tx_depth,
					user_param.rx_depth, user_param.ib_port, user_param.use_event,
					user_param.calc_opcode, user_param.calc_data_type, user_param.calc_data_size,
					user_param.verb);

			if (ret)
				return 1;
		}
		else
		{
			ret = pp_init_ctx_server(ctx,ib_dev, user_param.size, user_param.tx_depth,
								user_param.rx_depth, user_param.ib_port, user_param.use_event,
								user_param.calc_opcode, user_param.calc_data_type, user_param.calc_data_size,
								user_param.vector_count,
								// user_param.calc_operands_str,
								user_param.verb);
						if (ret)
							return 1;

		}
#else
		if (user_param.servername)		/*client*/
		{
			ret = pp_init_ctx(ctx,ib_dev, user_param.size, user_param.tx_depth,
					user_param.rx_depth, user_param.ib_port, user_param.use_event,
					user_param.verb);
			if (ret)
				return 1;
		}
		else
		{
			ret = pp_init_ctx_server(ctx,ib_dev, user_param.size, user_param.tx_depth,
								user_param.rx_depth, user_param.ib_port, user_param.use_event,
								user_param.verb);
						if (ret)
							return 1;

		}
#endif /* CALC_SUPPORT */



	///////////////server and vlient post recieve/////////////////////////////////

	routs = pp_post_recv(ctx, ctx->rx_depth);
	if (user_param.servername)		//client
	{
		if (routs < ctx->rx_depth) {
			fprintf(stderr, "Couldn't post receive (%d)\n", routs);
			return 1;
		}
	}
	else
	{
		if (routs < (ctx->rx_depth)*user_param.num_of_nodes) {
			fprintf(stderr, "Couldn't post receive (%d) for every node\n", routs);
			return 1;
		}
	}


	if (user_param.use_event) {
		if (ctx->cq && ibv_req_notify_cq(ctx->cq, 0)) {
			fprintf(stderr, "Couldn't request CQ notification\n");
			return 1;
		}
		if (ctx->tx_cq && ibv_req_notify_cq(ctx->tx_cq, 0)) {
			fprintf(stderr, "Couldn't request tx CQ notification\n");
			return 1;
		}
	}
	if (user_param.gid_index != -1) {
		if (ibv_query_gid(ctx->context,user_param.ib_port,user_param.gid_index,&temp_gid)) {
			return -1;
		}
	}

	if (pp_get_local_lid(ctx->context, user_param.ib_port, &my_dest[0].lid)) {
		fprintf(stderr, "Couldn't get local LID\n");
		return 1;
	}

	my_dest[0].qpn = ctx->qp[0]->qp_num;
	my_dest[0].psn = lrand48() & 0xffffff;

	// Each qp gives his receive buffer address .

	if (!user_param.servername) //server
	{
		my_dest[0].vaddr = (uintptr_t)ctx->net_buf[0];// + (user_param.num_of_qps + 0)*BUFF_SIZE(ctx->size);
		my_dest[0].rkey= ctx->mr[0]->rkey;
	} else {
		my_dest[0].vaddr = (uintptr_t)ctx->net_buf[1];
		my_dest[0].rkey= ctx->mr[1]->rkey;
	}

	my_dest[0].out_reads = user_param.out_reads;
	memcpy(my_dest[0].gid.raw,temp_gid.raw ,16);

	/*
	 * server need more dests.
	 */

	if (!user_param.servername) //server
	{
		for(j=1; j<user_param.num_of_nodes; j++)
		{
			if (pp_get_local_lid(ctx->context, user_param.ib_port, &my_dest[j].lid)) {
				fprintf(stderr, "Couldn't get local LID for node %d\n", j);
				return 1;
			}
			// my_dest[j].lid = pp_get_local_lid(ctx->context, user_param.ib_port);
			my_dest[j].qpn = ctx->qp[j]->qp_num;
			my_dest[j].psn = lrand48() & 0xffffff;

			// Each qp gives his receive buffer address .

			my_dest[j].vaddr = (uintptr_t)ctx->net_buf[j];// + (user_param.num_of_qps + i)*BUFF_SIZE(ctx->size); ///  i --> ?
			my_dest[j].rkey= ctx->mr[j]->rkey;
			my_dest[j].out_reads = user_param.out_reads;
			memcpy(my_dest[j].gid.raw,temp_gid.raw ,16);

			// if (!my_dest[j].lid) {
			// 	fprintf(stderr, "Couldn't get local LID\n");
			// 	return 1;
			// }
		}
	}

	if (!user_param.servername)  //server
		for(j=0; j<user_param.num_of_nodes; j++)
			printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, Rkey 0x%08x, Vadrr 0x%016llx\n",
					my_dest[j].lid, my_dest[j].qpn, my_dest[j].psn, my_dest[j].rkey, my_dest[j].vaddr);
	else
			printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, Rkey 0x%08x,Vadrr 0x%016llx\n",
							my_dest[0].lid, my_dest[0].qpn, my_dest[0].psn,my_dest[0].rkey, my_dest[0].vaddr);



	//////////////exvhange information between client and server//////////
	if (user_param.servername) // client
		rem_dest = pp_client_exch_dest(user_param.servername, user_param.port,
				&my_dest[0], &sockfd);
	else
		rem_dest = pp_server_exch_dest(
				ctx, user_param.ib_port, user_param.mtu,
				user_param.port, user_param.sl, my_dest, user_param.gid_index,
				user_param.num_of_nodes, connfd);

	if (!rem_dest) {
		fprintf(stderr, "Failed to exchange dest info\n");
		return 1;
	}

	if (!user_param.servername)	//server
		for(j=0; j<user_param.num_of_nodes; j++)
			printf("server: remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, Rkey 0x%08x,Vadrr 0x%016llx\n",
					rem_dest[j]->lid, rem_dest[j]->qpn, rem_dest[j]->psn,rem_dest[j]->rkey, rem_dest[j]->vaddr);
	else
		printf("client: remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, Rkey 0x%08x,Vadrr 0x%016llx\n",
							rem_dest[0]->lid, rem_dest[0]->qpn, rem_dest[0]->psn,rem_dest[0]->rkey, rem_dest[0]->vaddr);

	if (user_param.servername) 		//client
		if (pp_connect_ctx(ctx, ctx->qp[0], user_param.ib_port, my_dest[0].psn,
				user_param.mtu, user_param.sl, rem_dest[0], user_param.gid_index))
		{
			fprintf(stderr, "client failed to connect to server\n");
			return 1;
		}

	messaged("exchanged dest info\n");

	ctx->pending = PP_RECV_WRID;

	/************************** DONE INIT, START THE TEST **************************************/
	// server: produce list of WR: WAIT-SEND-WAIT-SEND on each qp-...
	if (!user_param.servername) /*server*/{
		int nodeind;
		messaged("server: pre-posting %d iterations\n", user_param.iters);
		if (server_pre_post_wqes(ctx, user_param.iters, rem_dest, user_param.num_of_nodes)) {
			fprintf(stderr, "Failed pre posting WQEs\n");
			return -1;
		}
		messaged("server: done pre-posting\n");

		 //send message to all the client that the prepost is done
		for (nodeind=0 ; nodeind< user_param.num_of_nodes; nodeind++) {
			if (write(connfd[nodeind], "ready", sizeof "ready") != sizeof "ready") {
				fprintf(stderr, "Couldn't write to node %d with ready notification\n", nodeind);
			}
			close(connfd[nodeind]);
		}
		messaged("server: done client notification\n");

	} else {
		//client waits until server is done preposting!
		if (read(sockfd, msg, sizeof ("ready")) != sizeof ("ready")) {
			// perror("client read with ready notification from server");
			fprintf(stderr, "client failed to read server msg: %s\n",
				msg);
			goto out;
		}

		close(sockfd);
		messaged("client: read server msg: %s\n", msg);
	}

	if (gettimeofday(&start, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	// main loop client: post write request, and poll on completion:
	if (!user_param.servername) {
		int ne_count = 0;

		//server poll until it gets all messages are received
		do {
			fflush(stdout);
			// polling tx cq
			ne = ibv_poll_cq(ctx->tx_cq, 1, wc);
			if (ne < 0) {
				fprintf(stderr, "poll tx CQ failed %d\n", ne);
				return 1;
			}
			if (ne > 0 ) {
				if  (wc->status != IBV_WC_SUCCESS) {
					fprintf(stderr,
						"poll tx CQ (%d) with wr_id=%lu "
						"returned with an error 0x%x vendor error 0x%x wc status %s\n",
						ne_count, wc->wr_id, wc->status, wc->vendor_err, ibv_wc_status_str(wc->status));
					return 1;
				}
			}
			ne_count = ne_count + ne;

			// // polling rx cq
			// ne = ibv_poll_cq(ctx->rx_cq, 1, wc);
			// if (ne < 0) {
			// 	fprintf(stderr, "poll rx CQ failed %d\n", ne);
			// 	return 1;
			// }
			// if (ne > 0 ) {
			// 	if  (wc->status != IBV_WC_SUCCESS) {
			// 		fprintf(stderr,
			// 			"poll rx CQ (%d) with wr_id=%lu "
			// 			"returned with an error 0x%x vendor error 0x%x wc status %s\n",
			// 			ne_count, wc->wr_id, wc->status, wc->vendor_err, ibv_wc_status_str(wc->status));
			// 		return 1;
			// 	}
			// }
			ne_count = ne_count + ne;
		} while (ne_count < (user_param.num_of_nodes*user_param.iters + user_param.num_of_nodes)); // TODO DEBUG 2 --> 1

		messaged("server: got %d completions\n", ne_count);
		fflush(stdout);
		messaged("ctrl+c to quit...\n");
		while(1);
	} else {
		int ne_count = 0;

		for (i = 0; i < user_param.iters; i++) {

			if (pp_post_send(ctx,rem_dest[0])) {
				fprintf(stderr, "Couldn't post send\n");
				return 1;
			}

			do {
				comp_count = ibv_poll_cq(ctx->cq, 1, wc);

				if (comp_count < 0) {
					fprintf(stderr, "poll CQ failed %d\n", comp_count);
					return 1;
				}
				if (comp_count > 0 ) {
					if  (wc->status != IBV_WC_SUCCESS) {
						fprintf(stderr, "poll CQ (%d) with wr_id=%lu returned with an error 0x%x : 0x%x - %s\n",
							ne_count, wc->wr_id, wc->status, wc->vendor_err, ibv_wc_status_str(wc->status));
						return 1;
					}

					printf("Received data (cqe->length %d, ctx->size:%d, operand_size:%d):\n",
						wc->byte_len,
						ctx->size, user_param.calc_data_size);

					dump_buffer(ctx, user_param.calc_data_type, user_param.calc_data_size, ctx->net_buf[1], wc->byte_len);
				}
			} while (comp_count < 1);
			ne_count = ne_count + comp_count;
		}

	}

	if (gettimeofday(&end, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	//print the result
	{
		float usec = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
		long long bytes = (long long) user_param.size * user_param.iters * 2;

		printf("%lld bytes in %.2f seconds = %.2f Mbit/sec\n", bytes, usec / 1000000., bytes * 8. / usec);
		printf("%d iters in %.2f seconds = %.2f usec/iter\n", user_param.iters, usec / 1000000., usec / user_param.iters);

		printf("\033[0;3%sm" "%s" "\033[m\n\n", "4", ">>>>LAUNCHED ON CORE-DIRECT API");

	}

	if (ctx->tx_cq)
		ibv_ack_cq_events(ctx->tx_cq, num_cq_events);

	if (ctx->cq)
		ibv_ack_cq_events(ctx->cq, num_cq_events);

	fflush(stdout);

out:

	if (pp_close_ctx(ctx))
		return 1;

	ibv_free_device_list(dev_list);

	return 0;
}
