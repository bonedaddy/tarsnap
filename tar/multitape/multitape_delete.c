#include "bsdtar_platform.h"

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "chunks.h"
#include "crypto.h"
#include "multitape_internal.h"
#include "storage.h"

#include "multitape.h"

static int callback_delete(void *, struct chunkheader *);

/**
 * callback_delete(cookie, ch):
 * Call chunks_delete_chunk on the chunk delete cookie ${cookie} and the
 * chunk header ${ch}.
 */
static int
callback_delete(void * cookie, struct chunkheader * ch)
{
	CHUNKS_D * C = cookie;

	return (chunks_delete_chunk(C, ch->hash));
}

/**
 * deletetape(machinenum, cachedir, tapename, printstats):
 * Delete the specified tape, and print statistics to stderr if requested.
 */
int
deletetape(uint64_t machinenum, const char * cachedir,
    const char * tapename, int printstats)
{
	struct tapemetadata tmd;
	CHUNKS_D * C;		/* Chunk layer delete cookie. */
	STORAGE_D * S;		/* Storage layer delete cookie. */
	STORAGE_R * SR;		/* Storage layer read cookie. */
	int lockfd;
	uint8_t lastseq[32];
	uint8_t seqnum[32];

	/* Lock the cache directory. */
	if ((lockfd = multitape_lock(cachedir)) == -1)
		goto err0;

	/* Make sure the lower layers are in a clean state. */
	if (multitape_cleanstate(cachedir, machinenum, 1))
		goto err1;

	/* Get sequence number (# of last committed transaction). */
	if (multitape_sequence(cachedir, lastseq))
		goto err1;

	/* Obtain storage and chunk layer cookies. */
	if ((S = storage_delete_start(machinenum, lastseq, seqnum)) == NULL)
		goto err1;
	if ((SR = storage_read_init(machinenum)) == NULL)
		goto err2;
	if ((C = chunks_delete_start(cachedir, S)) == NULL)
		goto err3;

	/* Read archive metadata. */
	if (multitape_metadata_get_byname(SR, NULL, &tmd, tapename, 0))
		goto err4;

	/* Delete chunks. */
	if (multitape_chunkiter_tmd(SR, NULL, &tmd, callback_delete, C, 0))
		goto err5;

	/* Delete archive index. */
	if (multitape_metaindex_delete(S, C, &tmd))
		goto err5;

	/* Delete archive metadata. */
	if (multitape_metadata_delete(S, C, &tmd))
		goto err5;

	/* Free tape metadata. */
	multitape_metadata_free(&tmd);

	/* Ask the storage layer to flush all pending deletes. */
	if (storage_delete_flush(S))
		goto err4;

	/* Print statistics if they were requested. */
	if ((printstats != 0) &&
	    chunks_delete_printstats(stderr, C))
		goto err4;

	/* Close storage and chunk layer cookies. */
	if (chunks_delete_end(C))
		goto err3;
	storage_read_free(SR);
	if (storage_delete_end(S))
		goto err1;

	/* Commit the transaction. */
	if (multitape_commit(cachedir, machinenum, seqnum, 1))
		goto err1;

	/* Unlock the cache directory. */
	close(lockfd);

	/* Success! */
	return (0);

err5:
	multitape_metadata_free(&tmd);
err4:
	chunks_delete_free(C);
err3:
	storage_read_free(SR);
err2:
	storage_delete_free(S);
err1:
	close(lockfd);
err0:
	/* Failure! */
	return (-1);
}