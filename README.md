# llreplace
OSX / Linux / DOS  File find and replace text command line tool.

LLReplace searches file text data using regular expression and replaces matches with substitution text.

  [![Build status](https://travis-ci.org/landenlabs/llreplace.svg?branch=master)](https://travis-ci.org/landenlabs/llreplace)
  [![Known Vulnerabilities](https://snyk.io/test/github/landenlabs/llreplace/badge.svg)](https://snyk.io/test/github/landenlabs/llreplace)

  
**Help screen**

<pre>
llreplace  Dennis Lang v2.5 (LandenLabs.com) Dec 28 2024

Des: Replace text in files
Use: llreplace [options] directories...

Main options:
   -from=<regExpression>          ; Pattern to find
   -till<regExpression>           ;   Optional end pattern to find
   -until<regExpression>          ;   Optional end pattern to find
   -to=<regExpression or string>  ; Optional replacment
   -backupDir=<directory>         ; Optional Path to store backup copy before change
   -out= - | outfilepath          ; Optional alternate output, default is input file

   -includeFile=<filePattern>     ; Include files by regex match
   -excludeFile=<filePattern>     ; Exclude files by regex match
   -IncludePath=<pathPattern>     ; Include path by regex match
   -ExcludePath=<pathPattern>     ; Exclude path by regex match
   -range=beg,end                 ; Optional line range filter

   directories...                 ; Directories to scan

Other options:
   -ignoreCase                    ; Next pattern set to ignore case
   -force                         ; Allow updates on read-only files
   -no                            ; Dry run, show changes if replacing
   -inverse                       ; Invert Search, show files not matching
   -maxFileSize=<#MB>             ; Max file size MB, def= 200
   -binary                        ; Include binary files
   -threads                       ; Search/Replace using 20 threads
   -threads=<#threads>            ; Search/Replace using threads

PrintFmt:
   -printFmt=' %r/%f(%o) %l\n'    ; Printf format to present match
    Each special character can include minWidth.maxWidth
       %10s     = pad out to 10 wide minimum
       %10.10s  = pad out to 10 wide min and clip to max of 10
       Without min or max, use entire value

       %s = entire file path
       %p = just directory path
       %r = relative directory path
       %n = file name only (no extension)
       %e = extension
       %f = filename with extension
       %o = offset into file where match found
       %z = match length
       %m = matched string
       %l = matched line  (colorized output)
  ex: -%printFmt='%20.20f %08o\n'
  Filename padded to 20 characters, max 20, and offset 8 digits leading zeros.

NOTES:
   . (dot) does not match \r \n,  you need to use [\r\n] or  (.|\r|\n)*
   Use lookahead for negative condition with dot, ex: "(?!&lt;/section)."  Full patter below
   Use single quotes to wrap from and/or to patterns if they use special characters
   like $ dollar sign to prevent shell from interception it.

Examples
 Search only, show patterns and defaults showing file and match:
  llreplace -from='Copyright' '-include=*.java' -print='%r/%f\n' src1 src2
  llreplace -from='Copyright' '-include=*.java' -include='*.xml' -print='%s' -inverse src res
  llreplace '-from=if [(]MapConfigInfo.DEBUG[)] [{][\r\n ]*Log[.](d|e)([(][^)]*[)];)[\r\n ]*[}]'  '-include=*.java' -range=0,10 -range=20,-1 -printFmt='%f %03d: ' src1 src2
  llreplace -printFmt="%m\n" -from="<section id='trail-stats'>((?!&lt;/section).|\r|\n)*</section>"

 Search and replace in-place:
  llreplace '-from=if [(]MapConfigInfo.DEBUG[)] [{][\r\n ]*Log[.](d|e)([(][^)]*[)];)[\r\n ]*[}]' '-to=MapConfigInfo.$1$2$3' '-include=*.java' src
  llreplace '-from=<block>' -till='</block>' '-to=' '-include=*.xml' res
  
</pre>


Visit home website

[https://landenlabs.com](https://landenlabs.com)

