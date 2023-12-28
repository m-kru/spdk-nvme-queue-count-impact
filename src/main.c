#include <time.h>

#include <spdk/nvme.h>

#include "log.h"
#include "panic.h"

#define BLOCK_SIZE (512) // NVMe single block size in bytes.
#define BLOCK_COUNT (1024 * 1024 * 1024)
#define BLOCKS_PER_WRITE (1)

#define NVME_QUEUE_COUNT (1)

volatile static uint64_t nvme_lba; // Current disk LBA

static struct spdk_nvme_transport_id nvme_trid = {};
static struct spdk_nvme_ctrlr *nvme_ctrlr;
const static struct spdk_nvme_ctrlr_data *nvme_ctrlr_data;
static struct spdk_nvme_ns *nvme_ns;
static struct spdk_nvme_qpair *nvme_qpairs[NVME_QUEUE_COUNT];
static unsigned nvme_qpair_indexes[NVME_QUEUE_COUNT];
static void *nvme_buf;

static bool nvme_probe_cb(
	void *cb_ctx,
	const struct spdk_nvme_transport_id *trid,
	struct spdk_nvme_ctrlr_opts *opts)
{
	info("nvme attaching to %s", trid->traddr);
	return true;
}

static void nvme_attach_cb(
	void *cb_ctx,
	const struct spdk_nvme_transport_id *trid,
	struct spdk_nvme_ctrlr *ctrlr,
	const struct spdk_nvme_ctrlr_opts *opts)
{
	info("nvme attached to %s", trid->traddr);
	nvme_ctrlr = ctrlr;
	nvme_ctrlr_data = spdk_nvme_ctrlr_get_data(ctrlr);

	// The disk under benchmark has only one namespace
	nvme_ns = spdk_nvme_ctrlr_get_ns(nvme_ctrlr, 1);
	if (!nvme_ns)
		panic("cannot get nvme namespace 1");
	if (!spdk_nvme_ns_is_active(nvme_ns))
		panic("namespace 1 is inactive");

	info(
		"  Namespace ID: %d size: %juGB",
		spdk_nvme_ns_get_id(nvme_ns),
		spdk_nvme_ns_get_size(nvme_ns) / 1000000000
	);
}

static void nvme_init(void) {
	struct spdk_env_opts opts;
	spdk_env_opts_init(&opts);
	opts.name = "spdk-nvme-queues";
	if (spdk_env_init(&opts) < 0) {
		panic("unable to initialize SPDK env");
	}

	spdk_nvme_trid_populate_transport(&nvme_trid, SPDK_NVME_TRANSPORT_PCIE);
	snprintf(nvme_trid.subnqn, sizeof(nvme_trid.subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);

	int err = spdk_nvme_probe(
		&nvme_trid, NULL, nvme_probe_cb, nvme_attach_cb, NULL
	);
	if (err)
		panic("spdk_nvme_probe() failed: %d", err);

	for (unsigned i = 0; i < NVME_QUEUE_COUNT; i++) {
		nvme_qpairs[i] = spdk_nvme_ctrlr_alloc_io_qpair(nvme_ctrlr, NULL, 0);
		if (!nvme_qpairs[i])
			panic("%d: spdk_nvme_ctrlr_alloc_io_qpair() failed", i);
		nvme_qpair_indexes[i] = i;
	}

	nvme_buf = spdk_zmalloc(BLOCK_SIZE * BLOCKS_PER_WRITE, 0, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
	if (!nvme_buf)
		panic("cannot allocate spdk buf");
}

static void nvme_write(unsigned queue_idx);

static void nvme_write_cb(void *arg, const struct spdk_nvme_cpl *cpl) {
	unsigned queue_idx = *(unsigned *)arg;

	if (spdk_nvme_cpl_is_error(cpl)) {
		spdk_nvme_qpair_print_completion(nvme_qpairs[queue_idx], (struct spdk_nvme_cpl *)cpl);
		panic(
			"spkd nvme write error: %s",
			spdk_nvme_cpl_get_status_string(&cpl->status)
		);
	}
	nvme_write(queue_idx);
}

static void nvme_write(unsigned queue_idx) {
	if (nvme_lba >= BLOCK_COUNT)
		return;

	const int err = spdk_nvme_ns_cmd_write(
		nvme_ns, nvme_qpairs[queue_idx], nvme_buf,
		nvme_lba, BLOCKS_PER_WRITE,
		nvme_write_cb, &nvme_qpair_indexes[queue_idx], 0
	);
	if (err)
		panic("%d: spdk_nvme_ns_cmd_write() failed: %d", queue_idx, err);

	nvme_lba += BLOCKS_PER_WRITE;
}

int main(int argc, char *argv[]) {
	nvme_init();

	struct timespec start_time, end_time;
	int err;

	err = clock_gettime(CLOCK_MONOTONIC, &start_time);
	if (err)
		panic("can't get start time: %s", strerror(err));

	for (unsigned i = 0; i < NVME_QUEUE_COUNT; i++) {
		nvme_write(i);
	}

	if (nvme_lba != NVME_QUEUE_COUNT * BLOCKS_PER_WRITE)
		error("race condition occured, ignoring this fact");

	// Wait for completion
	while (nvme_lba < BLOCK_COUNT) {}

	err = clock_gettime(CLOCK_MONOTONIC, &end_time);
	if (err)
		panic("can't get end time: %s", strerror(err));

	const long time_delta_ns =
		(end_time.tv_sec - start_time.tv_sec) * 1000000000 +
		(end_time.tv_nsec - start_time.tv_nsec);

	const long bandwidth = (long)BLOCK_SIZE * (long)BLOCK_COUNT / time_delta_ns / 1000000000;

	info("bandwidth: %" PRIu64 " B/s", bandwidth);

	return 0;
}
