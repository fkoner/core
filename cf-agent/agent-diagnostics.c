#include "agent-diagnostics.h"

#include "alloc.h"
#include "crypto.h"
#include "files_interfaces.h"
#include "string_lib.h"
#include "bootstrap.h"
#include "dbm_api.h"
#include "dbm_priv.h"

#include <assert.h>

AgentDiagnosticsResult AgentDiagnosticsResultNew(bool success, char *message)
{
    return (AgentDiagnosticsResult) { success, message };
}

static void AgentDiagnosticsResultDestroy(AgentDiagnosticsResult result)
{
    free(result.message);
}

void AgentDiagnosticsRun(const char *workdir, const AgentDiagnosticCheck checks[], Writer *output)
{
    {
        char diagnostics_path[CF_BUFSIZE] = { 0 };
        snprintf(diagnostics_path, CF_BUFSIZE, "%s/diagnostics", workdir);
        MapName(diagnostics_path);

        struct stat sb;
        if (cfstat(diagnostics_path, &sb) != 0)
        {
            if (mkdir(diagnostics_path, DEFAULTMODE) != 0)
            {
                WriterWriteF(output, "Cannot create diagnostics output directory '%s'", diagnostics_path);
                return;
            }
        }
    }


    for (int i = 0; checks[i].description; i++)
    {
        AgentDiagnosticsResult result = checks[i].check(workdir);
        WriterWriteF(output, "[ %s ] %s: %s\n",
                     result.success ? "YES" : "NO ",
                     checks[i].description,
                     result.message);
        AgentDiagnosticsResultDestroy(result);
    }
}

AgentDiagnosticsResult AgentDiagnosticsCheckIsBootstrapped(const char *workdir)
{
    char *policy_server = GetPolicyServer(workdir);
    return AgentDiagnosticsResultNew(policy_server != NULL,
                                     policy_server != NULL ? policy_server : xstrdup("Not bootstrapped"));
}

AgentDiagnosticsResult AgentDiagnosticsCheckAmPolicyServer(const char *workdir)
{
    bool am_policy_server = GetAmPolicyServer(workdir);
    return AgentDiagnosticsResultNew(am_policy_server,
                                     am_policy_server ? xstrdup("Acting as a policy server") : xstrdup("Not acting as a policy server"));
}

AgentDiagnosticsResult AgentDiagnosticsCheckPrivateKey(const char *workdir)
{
    const char *path = PrivateKeyFile(workdir);
    assert(path);
    struct stat sb;

    if (cfstat(path, &sb) != 0)
    {
        return AgentDiagnosticsResultNew(false, StringFormat("No private key found at '%s'", path));
    }

    if (sb.st_mode != (S_IFREG | S_IWUSR | S_IRUSR))
    {
        return AgentDiagnosticsResultNew(false, StringFormat("Private key found at '%s', but had incorrect permissions '%o'", path, sb.st_mode));
    }

    return AgentDiagnosticsResultNew(true, StringFormat("OK at '%s'", path));
}

AgentDiagnosticsResult AgentDiagnosticsCheckPublicKey(const char *workdir)
{
    const char *path = PublicKeyFile(workdir);
    assert(path);
    struct stat sb;

    if (cfstat(path, &sb) != 0)
    {
        return AgentDiagnosticsResultNew(false, StringFormat("No public key found at '%s'", path));
    }

    if (sb.st_mode != (S_IFREG | S_IWUSR | S_IRUSR))
    {
        return AgentDiagnosticsResultNew(false, StringFormat("Public key found at '%s', but had incorrect permissions '%o'", path, sb.st_mode));
    }

    if (sb.st_size != 426)
    {
        return AgentDiagnosticsResultNew(false, StringFormat("Public key at '%s' had size %zd bytes, expected 426 bytes", path, sb.st_size));
    }

    return AgentDiagnosticsResultNew(true, StringFormat("OK at '%s'", path));
}

static AgentDiagnosticsResult AgentDiagnosticsCheckDB(const char *workdir, dbid id)
{
    char *dbpath = DBIdToPath(workdir, id);
    char *error = DBPrivDiagnose(dbpath);
    free(dbpath);

    if (error)
    {
        return AgentDiagnosticsResultNew(false, error);
    }
    else
    {
        return AgentDiagnosticsResultNew(true, xstrdup("OK"));
    }
}

AgentDiagnosticsResult AgentDiagnosticsCheckDBPersistentClasses(const char *workdir)
{
    return AgentDiagnosticsCheckDB(workdir, dbid_state);
}

AgentDiagnosticsResult AgentDiagnosticsCheckDBChecksums(const char *workdir)
{
    return AgentDiagnosticsCheckDB(workdir, dbid_checksums);
}

AgentDiagnosticsResult AgentDiagnosticsCheckDBClasses(const char *workdir)
{
    return AgentDiagnosticsCheckDB(workdir, dbid_classes);
}

AgentDiagnosticsResult AgentDiagnosticsCheckDBLastSeen(const char *workdir)
{
    return AgentDiagnosticsCheckDB(workdir, dbid_lastseen);
}

AgentDiagnosticsResult AgentDiagnosticsCheckDBObservations(const char *workdir)
{
    return AgentDiagnosticsCheckDB(workdir, dbid_observations);
}

AgentDiagnosticsResult AgentDiagnosticsCheckDBFileStats(const char *workdir)
{
    return AgentDiagnosticsCheckDB(workdir, dbid_filestats);
}

AgentDiagnosticsResult AgentDiagnosticsCheckDBLocks(const char *workdir)
{
    return AgentDiagnosticsCheckDB(workdir, dbid_locks);
}

AgentDiagnosticsResult AgentDiagnosticsCheckDBPerformance(const char *workdir)
{
    return AgentDiagnosticsCheckDB(workdir, dbid_performance);
}

const AgentDiagnosticCheck *AgentDiagnosticsAllChecks(void)
{
    static const AgentDiagnosticCheck checks[] =
    {
        { "Check that agent is bootstrapped", &AgentDiagnosticsCheckIsBootstrapped },
        { "Check if agent is acting as a policy server", &AgentDiagnosticsCheckAmPolicyServer },
        { "Check private key", &AgentDiagnosticsCheckPrivateKey },
        { "Check public key", &AgentDiagnosticsCheckPublicKey },

        { "Check persistent classes DB", &AgentDiagnosticsCheckDBPersistentClasses },
        { "Check checksums DB", &AgentDiagnosticsCheckDBChecksums },
        { "Check classes DB", &AgentDiagnosticsCheckDBClasses },
        { "Check observations DB", &AgentDiagnosticsCheckDBObservations },
        { "Check file stats DB", &AgentDiagnosticsCheckDBFileStats },
        { "Check locks DB", &AgentDiagnosticsCheckDBLocks },
        { "Check performance DB", &AgentDiagnosticsCheckDBPerformance },

        { NULL, NULL }
    };

    return checks;
}
