#include <unistd.h>
#include <fcntl.h>

#include "postgres.h"
#include "access/heapam_xlog.h"
#include "access/xlog.h"
#include "access/xlog_internal.h"
#include "access/xlogprefetcher.h"
#include "access/xlogutils.h"
#include "common/controldata_utils.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "nodes/execnodes.h"
#include "postmaster/bgwriter.h"
#include "storage/fd.h"
#include "utils/builtins.h"
#include "utils/elog.h"

PG_MODULE_MAGIC;

#define MAXPATHLEN 1024

static int read_xlog_page(XLogReaderState *xlogreader, XLogRecPtr targetPagePtr, int reqLen, XLogRecPtr targetRecPtr, char *readBuf);

Datum recover(PG_FUNCTION_ARGS);
Datum show_records(PG_FUNCTION_ARGS);

typedef struct XLogPageReadPrivate
{
    char open_xlog_fd;
    BlockNumber blks; /* Blocks read */
    XLogSegNo seg_no;
    char path_name[MAXPATHLEN];
    TimeLineID replayTLI;

} XLogPageReadPrivate;

static int read_xlog_page(XLogReaderState *xlogreader, XLogRecPtr targetPagePtr, int reqLen, XLogRecPtr targetRecPtr, char *readBuf)
{
    XLogPageReadPrivate *private = (XLogPageReadPrivate *)xlogreader->private_data;
    uint32 target_page_off = XLogSegmentOffset(targetPagePtr, wal_segment_size);
    int read_len;
    XLogPageHeader hdr;

    if (lseek(private->open_xlog_fd, (off_t)target_page_off, SEEK_SET) < 0)
    {
        elog(WARNING, "unable to seek file \"%s\" desc %i", private->path_name, private->open_xlog_fd);
        return -1;
    }

    read_len = read(private->open_xlog_fd, readBuf, XLOG_BLCKSZ);
    if (read_len > XLOG_BLCKSZ)
    {
        ereport(WARNING,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("could not read from WAL segment %s, LSN %X/%X, offset %lu: read %d of %zu",
                        private->path_name, LSN_FORMAT_ARGS(targetPagePtr),
                        targetPagePtr, read_len, (Size)XLOG_BLCKSZ)));
        return -1;
    }

    private->blks++;

    hdr = (XLogPageHeader)readBuf;

    if (!XLogReaderValidatePageHeader(xlogreader, targetPagePtr, (char *)hdr))
    {
        elog(WARNING, "Invalid page header errorMsg: %s, page off: %u, recPtr: %X/%X, pagePtr: %zu, pageAdrr: %zu, info: %u, rem length: %u",
             xlogreader->errormsg_buf, target_page_off, LSN_FORMAT_ARGS(targetPagePtr),
             targetPagePtr, hdr->xlp_pageaddr, hdr->xlp_info, hdr->xlp_rem_len);
        return -1;
    }

    xlogreader->seg.ws_segno = private->seg_no;
    xlogreader->segoff = target_page_off;
    xlogreader->readLen = read_len;

    return read_len;
}

PG_FUNCTION_INFO_V1(recover);
Datum recover(PG_FUNCTION_ARGS)
{
#define XLOG_FIELD_NUM 2
    text *xlog_dir = PG_GETARG_TEXT_PP(0);
    ControlFileData *ControlFile = NULL;
    XLogReaderState *xlogreader = NULL;
    XLogPrefetcher *xlogprefetcher = NULL;
    XLogRecord *record = NULL;
    XLogSegNo seg_no;
    char *wal_path = NULL;
    int fd;
    int rc;
    HeapTuple tuple;
    TupleDesc tupdesc;
    char control_path[MAXPATHLEN];
    char fname[64];
    XLogPageReadPrivate private;
    char *errormsg;
    Datum values[XLOG_FIELD_NUM];
    bool nulls[XLOG_FIELD_NUM] = {false};
    XLogRecPtr RecPtr;
    Datum result;
    bool need_checkpoint;

    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        elog(ERROR, "return type must be a row type");

    wal_path = text_to_cstring(xlog_dir);
    snprintf(control_path, MAXPATHLEN, "%s/%s", wal_path, XLOG_CONTROL_FILE);

    fd = BasicOpenFile(control_path, O_RDWR | PG_BINARY);
    if (fd < 0)
    {
        /* Try default Control file path before we fail */
        memset(control_path, 0, MAXPATHLEN);
        wal_path = DataDir;
        snprintf(control_path, MAXPATHLEN, "%s/%s", wal_path, XLOG_CONTROL_FILE);
        fd = BasicOpenFile(control_path, O_RDWR | PG_BINARY);
        if (fd < 0)
            ereport(PANIC,
                    (errcode_for_file_access(), errmsg("open Control file error \"%s\": %m", XLOG_CONTROL_FILE)));
    }

    ControlFile = palloc(sizeof(ControlFileData));

    rc = read(fd, ControlFile, sizeof(ControlFileData));
    if (rc != sizeof(ControlFileData))
    {

        close(fd);
        pfree(ControlFile);
        ereport(PANIC,
                (errcode_for_file_access(), errmsg("read Control file error \"%s\": %m", XLOG_CONTROL_FILE)));
    }

    close(fd);

    if (ControlFile->checkPointCopy.redo == ControlFile->checkPoint)
    {
        pfree(ControlFile);
        elog(ERROR, "Database not in recovery");
    }

    XLByteToSeg(ControlFile->checkPointCopy.redo, seg_no, wal_segment_size);
    XLogFileName(fname, ControlFile->checkPointCopy.ThisTimeLineID, seg_no, wal_segment_size);

    snprintf(private.path_name, MAXPATHLEN, "%s/%s", wal_path, fname);

    private.replayTLI = ControlFile->checkPointCopy.ThisTimeLineID;
    private.seg_no = seg_no;

    private.open_xlog_fd = BasicOpenFile(private.path_name, O_RDONLY | PG_BINARY | O_CLOEXEC);
    if (private.open_xlog_fd < 0)
    {
        pfree(ControlFile);
        ereport(PANIC,
                (errcode_for_file_access(), errmsg("open Control file error \"%s\": %m", XLOG_CONTROL_FILE)));
    }

    xlogreader = XLogReaderAllocate(
        wal_segment_size,
        wal_path, XL_ROUTINE(.page_read = &read_xlog_page, .segment_open = NULL, .segment_close = wal_segment_close),
        &private);

    if (!xlogreader)
    {
        ereport(WARNING,
                (errcode(ERRCODE_OUT_OF_MEMORY),
                 errmsg("out of memory"),
                 errdetail("Failed while allocating a WAL reading processor.")));
        goto cleanup;
    }

    XLogReaderSetDecodeBuffer(xlogreader, NULL, wal_decode_buffer_size);
    xlogprefetcher = XLogPrefetcherAllocate(xlogreader);

    XLogPrefetcherBeginRead(xlogprefetcher, ControlFile->checkPointCopy.redo);

    RmgrStartup();

    for (;;)
    {
        RmgrData rmgr;

        record = XLogPrefetcherReadRecord(xlogprefetcher, &errormsg);
        if (record == NULL)
        {
            ereport(WARNING, (errmsg_internal("%s", errormsg)));
            break;
        }
        else
        {
            /*
             * Limit to DML ops only for now. Don't want to deal with timeline logic at this moment
             */
            if ((XLogRecGetInfo(xlogreader) & ~XLR_INFO_MASK) & XLOG_HEAP_OPMASK)
            {
                if (!RmgrIdExists(record->xl_rmid))
                {
                    elog(WARNING, "Resource manager does not exist for record at: %X/%X", LSN_FORMAT_ARGS(xlogreader->ReadRecPtr));
                    break;
                }
                rmgr = RmgrTable[record->xl_rmid];

                AdvanceNextFullTransactionIdPastXid(record->xl_xid);

                rmgr.rm_redo(xlogreader);
                RecPtr = xlogreader->ReadRecPtr;
                need_checkpoint = true;
            }
        }
    }

    /* Show the last record processed */
    if (record)
    {
        char res[64];
        RmgrData rmgr;
        uint8 info = record->xl_info & ~XLR_INFO_MASK;

        rmgr = RmgrTable[record->xl_rmid];
        values[0] = CStringGetTextDatum(rmgr.rm_identify(info));
        snprintf(res, sizeof(res), "%X/%X", LSN_FORMAT_ARGS(xlogreader->ReadRecPtr));
        values[1] = CStringGetTextDatum(res);
    }
    else
    {
        nulls[0] = true;
        nulls[1] = true;
    }

    RmgrCleanup();

    if (need_checkpoint)
    {
        if (!XLogRecPtrIsInvalid(RecPtr))
        {
            ControlFile->checkPointCopy.redo = RecPtr;
            update_controlfile(wal_path, ControlFile, true);
        }

        RequestCheckpoint(CHECKPOINT_END_OF_RECOVERY | CHECKPOINT_IMMEDIATE | CHECKPOINT_WAIT);
    }

    tuple = heap_form_tuple(tupdesc, values, nulls);
    result = HeapTupleGetDatum(tuple);

cleanup:
    pfree(ControlFile);
    XLogReaderFree(xlogreader);
    close(private.open_xlog_fd);

    PG_RETURN_DATUM(result);
}

PG_FUNCTION_INFO_V1(show_records);
Datum show_records(PG_FUNCTION_ARGS)
{
#ifdef XLOG_FIELD_NUM
#undef XLOG_FIELD_NUM
#endif
#define XLOG_FIELD_NUM 3
    text *xlog_dir = PG_GETARG_TEXT_PP(0);
    ControlFileData *ControlFile = NULL;
    XLogReaderState *xlogreader = NULL;
    XLogPrefetcher *xlogprefetcher = NULL;
    XLogRecord *record;
    XLogSegNo seg_no;
    ReturnSetInfo *rsinfo = (ReturnSetInfo *)fcinfo->resultinfo;
    char *wal_path = NULL;
    int fd;
    int rc;
    char control_path[MAXPATHLEN];
    char fname[64];
    XLogPageReadPrivate private;
    char *errormsg;
    Datum values[XLOG_FIELD_NUM];
    bool nulls[XLOG_FIELD_NUM] = {false};

    InitMaterializedSRF(fcinfo, 0);

    wal_path = text_to_cstring(xlog_dir);
    snprintf(control_path, MAXPATHLEN, "%s/%s", wal_path, XLOG_CONTROL_FILE);

    fd = BasicOpenFile(control_path, O_RDWR | PG_BINARY);
    if (fd < 0)
    {
        /* Try default Control file path before we fail */
        memset(control_path, 0, MAXPATHLEN);
        snprintf(control_path, MAXPATHLEN, "%s/%s", DataDir, XLOG_CONTROL_FILE);
        fd = BasicOpenFile(control_path, O_RDWR | PG_BINARY);
        if (fd < 0)
            ereport(PANIC,
                    (errcode_for_file_access(), errmsg("open Control file error \"%s\": %m", XLOG_CONTROL_FILE)));
    }

    ControlFile = palloc(sizeof(ControlFileData));

    rc = read(fd, ControlFile, sizeof(ControlFileData));
    if (rc != sizeof(ControlFileData))
    {

        close(fd);
        pfree(ControlFile);
        ereport(PANIC,
                (errcode_for_file_access(), errmsg("read Control file error \"%s\": %m", XLOG_CONTROL_FILE)));
    }

    close(fd);

    if (ControlFile->checkPointCopy.redo == ControlFile->checkPoint)
    {
        pfree(ControlFile);
        elog(ERROR, "Database not in recovery");
    }

    XLByteToSeg(ControlFile->checkPointCopy.redo, seg_no, wal_segment_size);
    XLogFileName(fname, ControlFile->checkPointCopy.ThisTimeLineID, seg_no, wal_segment_size);

    snprintf(private.path_name, MAXPATHLEN, "%s/%s", wal_path, fname);

    private.replayTLI = ControlFile->checkPointCopy.ThisTimeLineID;
    private.seg_no = seg_no;

    private.open_xlog_fd = BasicOpenFile(private.path_name, O_RDONLY | PG_BINARY | O_CLOEXEC);
    if (private.open_xlog_fd < 0)
    {
        pfree(ControlFile);
        ereport(PANIC,
                (errcode_for_file_access(), errmsg("open Control file error \"%s\": %m", XLOG_CONTROL_FILE)));
    }

    xlogreader = XLogReaderAllocate(
        wal_segment_size,
        wal_path, XL_ROUTINE(.page_read = &read_xlog_page, .segment_open = NULL, .segment_close = wal_segment_close),
        &private);

    if (!xlogreader)
    {
        ereport(WARNING,
                (errcode(ERRCODE_OUT_OF_MEMORY),
                 errmsg("out of memory"),
                 errdetail("Failed while allocating a WAL reading processor.")));
        goto cleanup;
    }

    XLogReaderSetDecodeBuffer(xlogreader, NULL, wal_decode_buffer_size);
    xlogprefetcher = XLogPrefetcherAllocate(xlogreader);

    XLogPrefetcherBeginRead(xlogprefetcher, ControlFile->checkPointCopy.redo);

    RmgrStartup();

    for (;;)
    {
        char res[64];
        RmgrData rmgr;
        uint8 info;

        record = XLogPrefetcherReadRecord(xlogprefetcher, &errormsg);
        if (record == NULL)
        {
            ereport(WARNING, (errmsg_internal("%s", errormsg)));
            break;
        }
        else
        {
            info = record->xl_info & ~XLR_INFO_MASK;
            if (!RmgrIdExists(record->xl_rmid))
            {
                elog(WARNING, "Resource manager does not exist for record at: %X/%X", LSN_FORMAT_ARGS(xlogreader->ReadRecPtr));
                break;
            }
            rmgr = RmgrTable[record->xl_rmid];
            values[0] = CStringGetTextDatum(fname);
            values[1] = CStringGetTextDatum(rmgr.rm_identify(info));
            snprintf(res, sizeof(res), "%X/%X", LSN_FORMAT_ARGS(xlogreader->ReadRecPtr));
            values[2] = CStringGetTextDatum(res);
            tuplestore_putvalues(rsinfo->setResult, rsinfo->setDesc, values, nulls);
        }
    }

    RmgrCleanup();

cleanup:
    pfree(ControlFile);
    XLogReaderFree(xlogreader);
    close(private.open_xlog_fd);

    PG_RETURN_VOID();
}