/* 
   Copyright (C) Cfengine AS

   This file is part of Cfengine 3 - written and maintained by Cfengine AS.
 
   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 3.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License  
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of Cfengine, the applicable Commerical Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.

*/

#include "verify_files.h"

#include "promises.h"
#include "vars.h"
#include "dir.h"
#include "scope.h"
#include "env_context.h"
#include "files_names.h"
#include "files_interfaces.h"
#include "files_lib.h"
#include "files_operators.h"
#include "files_hashes.h"
#include "files_edit.h"
#include "files_editxml.h"
#include "files_editline.h"
#include "files_properties.h"
#include "item_lib.h"
#include "matching.h"
#include "attributes.h"
#include "logging.h"
#include "locks.h"
#include "string_lib.h"
#include "verify_files_utils.h"
#include "verify_files_hashes.h"
#include "generic_agent.h" // HashVariables
#include "misc_lib.h"
#include "fncall.h"
#include "promiser_regex_resolver.h"
#include "ornaments.h"
#include "audit.h"

#ifdef HAVE_NOVA
#include "cf.nova.h"
#endif

static void LoadSetuid(Attributes a);
static void SaveSetuid(EvalContext *ctx, Attributes a, Promise *pp);
static void FindFilePromiserObjects(EvalContext *ctx, Promise *pp);
static void VerifyFilePromise(EvalContext *ctx, char *path, Promise *pp);

/*****************************************************************************/

static int FileSanityChecks(const EvalContext *ctx, char *path, Attributes a, Promise *pp)
{
    if ((a.havelink) && (a.havecopy))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "",
              " !! Promise constraint conflicts - %s file cannot both be a copy of and a link to the source", path);
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.havelink) && (!a.link.source))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! Promise to establish a link at %s has no source", path);
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

/* We can't do this verification during parsing as we did not yet read the body,
 * so we can't distinguish between link and copy source. In post-verification
 * all bodies are already expanded, so we don't have the information either */

    if ((a.havecopy) && (a.copy.source) && (!FullTextMatch(CF_ABSPATHRANGE, a.copy.source)))
    {
        /* FIXME: somehow redo a PromiseRef to be able to embed it into a string */
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! Non-absolute path in source attribute (have no invariant meaning): %s", a.copy.source);
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        FatalError(ctx, "Bailing out");
    }

    if ((a.haveeditline) && (a.haveeditxml))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! Promise constraint conflicts - %s editing file as both line and xml makes no sense",
              path);
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.havedepthsearch) && (a.haveedit))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! Recursive depth_searches are not compatible with general file editing");
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.havedelete) && ((a.create) || (a.havecopy) || (a.haveedit) || (a.haverename)))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! Promise constraint conflicts - %s cannot be deleted and exist at the same time", path);
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.haverename) && ((a.create) || (a.havecopy) || (a.haveedit)))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "",
              " !! Promise constraint conflicts - %s cannot be renamed/moved and exist there at the same time", path);
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.havedelete) && (a.havedepthsearch) && (!a.haveselect))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "",
              " !! Dangerous or ambiguous promise - %s specifies recursive deletion but has no file selection criteria",
              path);
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.haveselect) && (!a.select.result))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! File select constraint body promised no result (check body definition)");
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.havedelete) && (a.haverename))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! File %s cannot promise both deletion and renaming", path);
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.havecopy) && (a.havedepthsearch) && (a.havedelete))
    {
        CfOut(OUTPUT_LEVEL_INFORM, "",
              " !! Warning: depth_search of %s applies to both delete and copy, but these refer to different searches (source/destination)",
              pp->promiser);
        PromiseRef(OUTPUT_LEVEL_INFORM, pp);
    }

    if ((a.transaction.background) && (a.transaction.audit))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! Auditing cannot be performed on backgrounded promises (this might change).");
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if (((a.havecopy) || (a.havelink)) && (a.transformer))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! File object(s) %s cannot both be a copy of source and transformed simultaneously",
              pp->promiser);
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.haveselect) && (a.select.result == NULL))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! Missing file_result attribute in file_select body");
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    if ((a.havedepthsearch) && (a.change.report_diffs))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", " !! Difference reporting is not allowed during a depth_search");
        PromiseRef(OUTPUT_LEVEL_ERROR, pp);
        return false;
    }

    return true;
}

static void VerifyFilePromise(EvalContext *ctx, char *path, Promise *pp)
{
    struct stat osb, oslb, dsb;
    Attributes a = { {0} };
    CfLock thislock;
    int exists;

    a = GetFilesAttributes(ctx, pp);

    if (!FileSanityChecks(ctx, path, a, pp))
    {
        return;
    }

    ScopeDeleteSpecialScalar("this", "promiser");
    ScopeNewSpecialScalar(ctx, "this", "promiser", path, DATA_TYPE_STRING);
    
    thislock = AcquireLock(ctx, path, VUQNAME, CFSTARTTIME, a.transaction, pp, false);

    if (thislock.lock == NULL)
    {
        return;
    }

    LoadSetuid(a);

    if (lstat(path, &oslb) == -1)       /* Careful if the object is a link */
    {
        if ((a.create) || (a.touch))
        {
            if (!CfCreateFile(ctx, path, pp, a))
            {
                goto exit;
            }
            else
            {
                exists = (lstat(path, &oslb) != -1);
            }
        }

        exists = false;
    }
    else
    {
        if ((a.create) || (a.touch))
        {
            cfPS(ctx, OUTPUT_LEVEL_VERBOSE, PROMISE_RESULT_NOOP, "", pp, a, " -> File \"%s\" exists as promised", path);
        }
        exists = true;
    }

    if ((a.havedelete) && (!exists))
    {
        cfPS(ctx, OUTPUT_LEVEL_VERBOSE, PROMISE_RESULT_NOOP, "", pp, a, " -> File \"%s\" does not exist as promised", path);
        goto exit;
    }

    if (!a.havedepthsearch)     /* if the search is trivial, make sure that we are in the parent dir of the leaf */
    {
        char basedir[CF_BUFSIZE];

        CfDebug(" -> Direct file reference %s, no search implied\n", path);
        snprintf(basedir, sizeof(basedir), "%s", path);

        if (strcmp(ReadLastNode(basedir), ".") == 0)
        {
            // Handle /.  notation for deletion of directories
            ChopLastNode(basedir);
            ChopLastNode(path);
        }

        ChopLastNode(basedir);
        if (chdir(basedir))
        {
            CfOut(OUTPUT_LEVEL_ERROR, "", "Failed to chdir into '%s'\n", basedir);
        }
    }

    if (exists && (!VerifyFileLeaf(ctx, path, &oslb, a, pp)))
    {
        if (!S_ISDIR(oslb.st_mode))
        {
            goto exit;
        }
    }

    if (cfstat(path, &osb) == -1)
    {
        if ((a.create) || (a.touch))
        {
            if (!CfCreateFile(ctx, path, pp, a))
            {
                goto exit;
            }
            else
            {
                exists = true;
            }
        }
        else
        {
            exists = false;
        }
    }
    else
    {
        if (!S_ISDIR(osb.st_mode))
        {
            if (a.havedepthsearch)
            {
                CfOut(OUTPUT_LEVEL_INFORM, "",
                      "Warning: depth_search (recursion) is promised for a base object %s that is not a directory",
                      path);
                goto exit;
            }
        }

        exists = true;
    }

    if (a.link.link_children)
    {
        if (cfstat(a.link.source, &dsb) != -1)
        {
            if (!S_ISDIR(dsb.st_mode))
            {
                CfOut(OUTPUT_LEVEL_ERROR, "", "Cannot promise to link the children of %s as it is not a directory!",
                      a.link.source);
                goto exit;
            }
        }
    }

/* Phase 1 - */

    if (exists && ((a.havedelete) || (a.haverename) || (a.haveperms) || (a.havechange) || (a.transformer)))
    {
        lstat(path, &oslb);     /* if doesn't exist have to stat again anyway */

        DepthSearch(ctx, path, &oslb, 0, a, pp, oslb.st_dev);

        /* normally searches do not include the base directory */

        if (a.recursion.include_basedir)
        {
            int save_search = a.havedepthsearch;

            /* Handle this node specially */

            a.havedepthsearch = false;
            DepthSearch(ctx, path, &oslb, 0, a, pp, oslb.st_dev);
            a.havedepthsearch = save_search;
        }
        else
        {
            /* unless child nodes were repaired, set a promise kept class */
            if (!IsDefinedClass(ctx, "repaired" , PromiseGetNamespace(pp)))
            {
                cfPS(ctx, OUTPUT_LEVEL_VERBOSE, PROMISE_RESULT_NOOP, "", pp, a, " -> Basedir \"%s\" not promising anything", path);
            }
        }

        if (((a.change.report_changes) == FILE_CHANGE_REPORT_CONTENT_CHANGE) || ((a.change.report_changes) == FILE_CHANGE_REPORT_ALL))
        {
            if (a.havedepthsearch)
            {
                PurgeHashes(ctx, NULL, a, pp);
            }
            else
            {
                PurgeHashes(ctx, path, a, pp);
            }
        }
    }

/* Phase 2a - copying is potentially threadable if no followup actions */

    if (a.havecopy)
    {
        ScheduleCopyOperation(ctx, path, a, pp);
    }

/* Phase 2b link after copy in case need file first */

    if ((a.havelink) && (a.link.link_children))
    {
        ScheduleLinkChildrenOperation(ctx, path, a.link.source, 1, a, pp);
    }
    else if (a.havelink)
    {
        ScheduleLinkOperation(ctx, path, a.link.source, a, pp);
    }

/* Phase 3 - content editing */

    if (a.haveedit)
    {
        ScheduleEditOperation(ctx, path, a, pp);
    }

// Once more in case a file has been created as a result of editing or copying

    if ((cfstat(path, &osb) != -1) && (S_ISREG(osb.st_mode)))
    {
        VerifyFileLeaf(ctx, path, &osb, a, pp);
    }

exit:
    SaveSetuid(ctx, a, pp);
    YieldCurrentLock(thislock);
}

/*****************************************************************************/

int ScheduleEditOperation(EvalContext *ctx, char *filename, Attributes a, Promise *pp)
{
    void *vp;
    FnCall *fp;
    char edit_bundle_name[CF_BUFSIZE], lockname[CF_BUFSIZE], qualified_edit[CF_BUFSIZE], *method_deref;
    Rlist *params = { 0 };
    int retval = false;
    CfLock thislock;

    snprintf(lockname, CF_BUFSIZE - 1, "fileedit-%s", filename);
    thislock = AcquireLock(ctx, lockname, VUQNAME, CFSTARTTIME, a.transaction, pp, false);

    if (thislock.lock == NULL)
    {
        return false;
    }

    EditContext *edcontext = NewEditContext(filename, a);

    if (edcontext == NULL)
    {
        cfPS(ctx, OUTPUT_LEVEL_ERROR, PROMISE_RESULT_FAIL, "", pp, a, "File %s was marked for editing but could not be opened\n", filename);
        retval = false;
        goto exit;
    }

    Policy *policy = PolicyFromPromise(pp);

    if (a.haveeditline)
    {
        if ((vp = ConstraintGetRvalValue(ctx, "edit_line", pp, RVAL_TYPE_FNCALL)))
        {
            fp = (FnCall *) vp;
            strcpy(edit_bundle_name, fp->name);
            params = fp->args;
        }
        else if ((vp = ConstraintGetRvalValue(ctx, "edit_line", pp, RVAL_TYPE_SCALAR)))
        {
            strcpy(edit_bundle_name, (char *) vp);
            params = NULL;
        }             
        else
        {
            retval = false;
            goto exit;
        }

        if (strncmp(edit_bundle_name,"default:",strlen("default:")) == 0) // CF_NS == ':'
        {
            method_deref = strchr(edit_bundle_name, CF_NS) + 1;
        }
        else if ((strchr(edit_bundle_name, CF_NS) == NULL) && (strcmp(PromiseGetNamespace(pp), "default") != 0))
        {
            snprintf(qualified_edit, CF_BUFSIZE, "%s%c%s", PromiseGetNamespace(pp), CF_NS, edit_bundle_name);
            method_deref = qualified_edit;
        }
        else            
        {
            method_deref = edit_bundle_name;
        }        

        CfOut(OUTPUT_LEVEL_VERBOSE, "", " -> Handling file edits in edit_line bundle %s\n", method_deref);

        Bundle *bp = NULL;
        if ((bp = PolicyGetBundle(policy, NULL, "edit_line", method_deref)))
        {
            BannerSubBundle(bp, params);

            EvalContextStackPushBundleFrame(ctx, bp, a.edits.inherit);
            ScopeClear(bp->name);
            BundleHashVariables(ctx, bp);
            ScopeAugment(ctx, bp, params);

            retval = ScheduleEditLineOperations(ctx, bp, a, pp, edcontext);

            EvalContextStackPopFrame(ctx);

            ScopeClear(bp->name);
        }
        else
        {
            printf("DIDN*T FIND %s ... %s \n", method_deref, edit_bundle_name);
        }
    }


    if (a.haveeditxml)
    {
        if ((vp = ConstraintGetRvalValue(ctx, "edit_xml", pp, RVAL_TYPE_FNCALL)))
        {
            fp = (FnCall *) vp;
            strcpy(edit_bundle_name, fp->name);
            params = fp->args;
        }
        else if ((vp = ConstraintGetRvalValue(ctx, "edit_xml", pp, RVAL_TYPE_SCALAR)))
        {
            strcpy(edit_bundle_name, (char *) vp);
            params = NULL;
        }
        else
        {
            retval = false;
            goto exit;
        }

        if (strncmp(edit_bundle_name,"default:",strlen("default:")) == 0) // CF_NS == ':'
        {
            method_deref = strchr(edit_bundle_name, CF_NS) + 1;
        }
        else
        {
            method_deref = edit_bundle_name;
        }
        
        CfOut(OUTPUT_LEVEL_VERBOSE, "", " -> Handling file edits in edit_xml bundle %s\n", method_deref);

        Bundle *bp = NULL;
        if ((bp = PolicyGetBundle(policy, NULL, "edit_xml", method_deref)))
        {
            BannerSubBundle(bp, params);

            EvalContextStackPushBundleFrame(ctx, bp, a.edits.inherit);
            ScopeClear(bp->name);
            BundleHashVariables(ctx, bp);
            ScopeAugment(ctx, bp, params);

            retval = ScheduleEditXmlOperations(ctx, bp, a, pp, edcontext);

            EvalContextStackPopFrame(ctx);

            ScopeClear(bp->name);
        }
    }

    
    if (a.template)
    {
        Policy *tmp_policy = PolicyNew();

        Bundle *bp = NULL;
        if ((bp = MakeTemporaryBundleFromTemplate(ctx, tmp_policy, a, pp)))
        {
            BannerSubBundle(bp,params);
            a.haveeditline = true;

            EvalContextStackPushBundleFrame(ctx, bp, a.edits.inherit);
            ScopeClear(bp->name);
            BundleHashVariables(ctx, bp);

            retval = ScheduleEditLineOperations(ctx, bp, a, pp, edcontext);

            EvalContextStackPopFrame(ctx);

            ScopeClear(bp->name);
        }

        PolicyDestroy(tmp_policy);
    }

exit:
    FinishEditContext(ctx, edcontext, a, pp);
    YieldCurrentLock(thislock);
    return retval;
}

/*****************************************************************************/

void *FindAndVerifyFilesPromises(EvalContext *ctx, Promise *pp)
{
    PromiseBanner(pp);
    FindFilePromiserObjects(ctx, pp);
    return (void *) NULL;
}

/*****************************************************************************/

static void FindFilePromiserObjects(EvalContext *ctx, Promise *pp)
{
    char *val = ConstraintGetRvalValue(ctx, "pathtype", pp, RVAL_TYPE_SCALAR);
    int literal = (PromiseGetConstraintAsBoolean(ctx, "copy_from", pp)) || ((val != NULL) && (strcmp(val, "literal") == 0));

/* Check if we are searching over a regular expression */

    if (literal)
    {
        // Prime the promiser temporarily, may override later
        ScopeNewSpecialScalar(ctx, "this", "promiser", pp->promiser, DATA_TYPE_STRING);
        VerifyFilePromise(ctx, pp->promiser, pp);
    }
    else                        // Default is to expand regex paths
    {
        LocateFilePromiserGroup(ctx, pp->promiser, pp, VerifyFilePromise);
    }
}

static void LoadSetuid(Attributes a)
{
    char filename[CF_BUFSIZE];

    EditDefaults edits = a.edits;
    edits.backup = BACKUP_OPTION_NO_BACKUP;
    edits.maxfilesize = 1000000;

    snprintf(filename, CF_BUFSIZE, "%s/cfagent.%s.log", CFWORKDIR, VSYSNAME.nodename);
    MapName(filename);

    if (!LoadFileAsItemList(&VSETUIDLIST, filename, edits))
    {
        CfOut(OUTPUT_LEVEL_VERBOSE, "", "Did not find any previous setuid log %s, creating a new one", filename);
    }
}

/*********************************************************************/

static void SaveSetuid(EvalContext *ctx, Attributes a, Promise *pp)
{
    Attributes b = a;

    b.edits.backup = BACKUP_OPTION_NO_BACKUP;
    b.edits.maxfilesize = 1000000;

    char filename[CF_BUFSIZE];
    snprintf(filename, CF_BUFSIZE, "%s/cfagent.%s.log", CFWORKDIR, VSYSNAME.nodename);
    MapName(filename);

    PurgeItemList(&VSETUIDLIST, "SETUID/SETGID");

    if (!CompareToFile(ctx, VSETUIDLIST, filename, a, pp))
    {
        SaveItemListAsFile(VSETUIDLIST, filename, b);
    }

    DeleteItemList(VSETUIDLIST);
    VSETUIDLIST = NULL;
}

