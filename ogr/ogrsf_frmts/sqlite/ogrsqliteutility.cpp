/******************************************************************************
 *
 * Project:  GeoPackage Translator
 * Purpose:  Utility functions for OGR GeoPackage driver.
 * Author:   Paul Ramsey, pramsey@boundlessgeo.com
 *
 ******************************************************************************
 * Copyright (c) 2013, Paul Ramsey <pramsey@boundlessgeo.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "cpl_port.h"
#include "ogrsqliteutility.h"

#include <cstdlib>
#include <string>

#include "cpl_error.h"
#include "ogr_p.h"

CPL_CVSID("$Id: ogrsqliteutility.cpp d7976d4611d69cb3b28a4d6cc623ec4e6826cae0 2019-10-28 09:12:28 +0100 Even Rouault $")

/* Runs a SQL command and ignores the result (good for INSERT/UPDATE/CREATE) */
OGRErr SQLCommand(sqlite3 * poDb, const char * pszSQL)
{
    CPLAssert( poDb != nullptr );
    CPLAssert( pszSQL != nullptr );

    char *pszErrMsg = nullptr;
#ifdef DEBUG_VERBOSE
    CPLDebug("GPKG", "exec(%s)", pszSQL);
#endif
    int rc = sqlite3_exec(poDb, pszSQL, nullptr, nullptr, &pszErrMsg);

    if ( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "sqlite3_exec(%s) failed: %s",
                  pszSQL, pszErrMsg ? pszErrMsg : "" );
        sqlite3_free( pszErrMsg );
        return OGRERR_FAILURE;
    }

    return OGRERR_NONE;
}

OGRErr SQLResultInit(SQLResult * poResult)
{
    poResult->papszResult = nullptr;
    poResult->pszErrMsg = nullptr;
    poResult->nRowCount = 0;
    poResult->nColCount = 0;
    poResult->rc = 0;
    return OGRERR_NONE;
}

OGRErr SQLQuery(sqlite3 * poDb, const char * pszSQL, SQLResult * poResult)
{
    CPLAssert( poDb != nullptr );
    CPLAssert( pszSQL != nullptr );
    CPLAssert( poResult != nullptr );

    SQLResultInit(poResult);

#ifdef DEBUG_VERBOSE
    CPLDebug("GPKG", "get_table(%s)", pszSQL);
#endif
    poResult->rc = sqlite3_get_table(
        poDb, pszSQL,
        &(poResult->papszResult),
        &(poResult->nRowCount),
        &(poResult->nColCount),
        &(poResult->pszErrMsg) );

    if( poResult->rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "sqlite3_get_table(%s) failed: %s", pszSQL, poResult->pszErrMsg );
        return OGRERR_FAILURE;
    }

    return OGRERR_NONE;
}

OGRErr SQLResultFree(SQLResult * poResult)
{
    if ( poResult->papszResult )
        sqlite3_free_table(poResult->papszResult);

    if ( poResult->pszErrMsg )
        sqlite3_free(poResult->pszErrMsg);

    return OGRERR_NONE;
}

const char* SQLResultGetValue(const SQLResult * poResult, int iColNum, int iRowNum)
{
    CPLAssert( poResult != nullptr );

    const int nCols = poResult->nColCount;
#ifdef DEBUG
    const int nRows = poResult->nRowCount;
    CPL_IGNORE_RET_VAL(nRows);

    CPLAssert( iColNum >= 0 && iColNum < nCols );
    CPLAssert( iRowNum >= 0 && iRowNum < nRows );
#endif
    return poResult->papszResult[ nCols + iRowNum * nCols + iColNum ];
}

int SQLResultGetValueAsInteger(const SQLResult * poResult, int iColNum, int iRowNum)
{
    const char *pszValue = SQLResultGetValue(poResult, iColNum, iRowNum);
    if ( ! pszValue )
        return 0;

    return atoi(pszValue);
}

/* Returns the first row of first column of SQL as integer */
GIntBig SQLGetInteger64(sqlite3 * poDb, const char * pszSQL, OGRErr *err)
{
    CPLAssert( poDb != nullptr );

    sqlite3_stmt *poStmt = nullptr;

    /* Prepare the SQL */
#ifdef DEBUG_VERBOSE
    CPLDebug("GPKG", "get(%s)", pszSQL);
#endif
    int rc = sqlite3_prepare_v2(poDb, pszSQL, -1, &poStmt, nullptr);
    if ( rc != SQLITE_OK )
    {
        CPLError( CE_Failure, CPLE_AppDefined, "sqlite3_prepare_v2(%s) failed: %s",
                  pszSQL, sqlite3_errmsg( poDb ) );
        if ( err ) *err = OGRERR_FAILURE;
        return 0;
    }

    /* Execute and fetch first row */
    rc = sqlite3_step(poStmt);
    if ( rc != SQLITE_ROW )
    {
        if ( err ) *err = OGRERR_FAILURE;
        sqlite3_finalize(poStmt);
        return 0;
    }

    /* Read the integer from the row */
    GIntBig i = sqlite3_column_int64(poStmt, 0);
    sqlite3_finalize(poStmt);

    if ( err ) *err = OGRERR_NONE;
    return i;
}

int SQLGetInteger(sqlite3 * poDb, const char * pszSQL, OGRErr *err)
{
    return static_cast<int>(SQLGetInteger64(poDb, pszSQL, err));
}

int SQLiteFieldFromOGR(OGRFieldType eType)
{
    switch(eType)
    {
        case OFTInteger:
            return SQLITE_INTEGER;
        case OFTReal:
            return SQLITE_FLOAT;
        case OFTString:
            return SQLITE_TEXT;
        case OFTBinary:
            return SQLITE_BLOB;
        case OFTDate:
            return SQLITE_TEXT;
        case OFTDateTime:
            return SQLITE_TEXT;
        default:
            return 0;
    }
}

/************************************************************************/
/*                             SQLUnescape()                            */
/************************************************************************/

CPLString SQLUnescape(const char* pszVal)
{
    char chQuoteChar = pszVal[0];
    if( chQuoteChar != '\'' && chQuoteChar != '"' )
        return pszVal;

    CPLString osRet;
    pszVal ++;
    while( *pszVal != '\0' )
    {
        if( *pszVal == chQuoteChar )
        {
            if( pszVal[1] == chQuoteChar )
                pszVal ++;
            else
                break;
        }
        osRet += *pszVal;
        pszVal ++;
    }
    return osRet;
}

/************************************************************************/
/*                          SQLEscapeLiteral()                          */
/************************************************************************/

CPLString SQLEscapeLiteral( const char *pszLiteral )
{
    CPLString osVal;
    for( int i = 0; pszLiteral[i] != '\0'; i++ )
    {
        if ( pszLiteral[i] == '\'' )
            osVal += '\'';
        osVal += pszLiteral[i];
    }
    return osVal;
}

/************************************************************************/
/*                           SQLEscapeName()                            */
/************************************************************************/

CPLString SQLEscapeName(const char* pszName)
{
    CPLString osRet;
    while( *pszName != '\0' )
    {
        if( *pszName == '"' )
            osRet += "\"\"";
        else
            osRet += *pszName;
        pszName ++;
    }
    return osRet;
}

/************************************************************************/
/*                             SQLTokenize()                            */
/************************************************************************/

char** SQLTokenize( const char* pszStr )
{
    char** papszTokens = nullptr;
    bool bInQuote = false;
    char chQuoteChar = '\0';
    bool bInSpace = true;
    CPLString osCurrentToken;
    while( *pszStr != '\0' )
    {
        if( *pszStr == ' ' && !bInQuote )
        {
            if( !bInSpace )
            {
                papszTokens = CSLAddString(papszTokens, osCurrentToken);
                osCurrentToken.clear();
            }
            bInSpace = true;
        }
        else if( (*pszStr == '(' || *pszStr == ')' || *pszStr == ',')  && !bInQuote )
        {
            if( !bInSpace )
            {
                papszTokens = CSLAddString(papszTokens, osCurrentToken);
                osCurrentToken.clear();
            }
            osCurrentToken.clear();
            osCurrentToken += *pszStr;
            papszTokens = CSLAddString(papszTokens, osCurrentToken);
            osCurrentToken.clear();
            bInSpace = true;
        }
        else if( *pszStr == '"' || *pszStr == '\'' )
        {
            if( bInQuote && *pszStr == chQuoteChar && pszStr[1] == chQuoteChar )
            {
                osCurrentToken += *pszStr;
                osCurrentToken += *pszStr;
                pszStr += 2;
                continue;
            }
            else if( bInQuote && *pszStr == chQuoteChar )
            {
                osCurrentToken += *pszStr;
                papszTokens = CSLAddString(papszTokens, osCurrentToken);
                osCurrentToken.clear();
                bInSpace = true;
                bInQuote = false;
                chQuoteChar = '\0';
            }
            else if( bInQuote )
            {
                osCurrentToken += *pszStr;
            }
            else
            {
                chQuoteChar = *pszStr;
                osCurrentToken.clear();
                osCurrentToken += chQuoteChar;
                bInQuote = true;
                bInSpace = false;
            }
        }
        else
        {
            osCurrentToken += *pszStr;
            bInSpace = false;
        }
        pszStr ++;
    }

    if( !osCurrentToken.empty() )
        papszTokens = CSLAddString(papszTokens, osCurrentToken);

    return papszTokens;
}
