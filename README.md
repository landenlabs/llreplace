<table border="0">
  <tr>
    <td>
      <!-- VERSION -->v6.07.10<br>
      <!-- DATE -->12-Jul-2026<br>
      macOS<br>
      <a href="https://landenlabs.com">Home</a>
    </td>
    <td>
      <a href="https://landenlabs.com">
        <img src="screens/landen_labs_300.webp" width="300" alt="LanDen Labs">
      </a>
    </td>
  </tr>
</table>

# llreplace
OSX / Linux / DOS  File find and replace text command line tool.

LLReplace searches file text data using regular expression and replaces matches with substitution text.

  [![Build status](https://travis-ci.org/landenlabs/llreplace.svg?branch=master)](https://travis-ci.org/landenlabs/llreplace)
  [![Known Vulnerabilities](https://snyk.io/test/github/landenlabs/llreplace/badge.svg)](https://snyk.io/test/github/landenlabs/llreplace)
  [![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE.txt)
  ![Language](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)
  ![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-lightgrey.svg)

### Dependencies
* [llcommon](https://github.com/landenlabs/llcommon) - shared LanDen Labs utility library (git submodule)

  
**Help screen**

<pre>


llreplace  Dennis Lang v2.9 (LandenLabs.com) Jan 25 2025

Des: Replace text in files
Use: llreplace [options] directories...

Main options:
   -from=&lt;regExpression>          ; Pattern to find
   -till&lt;regExpression>           ;   Optional end pattern to find
   -until&lt;regExpression>          ;   Optional end pattern to find
   -to=&lt;regExpression or string>  ; Optional replacment
   -backupDir=&lt;directory>         ; Optional Path to store backup copy before change
   -out= - | outfilepath          ; Optional alternate output, default is input file

   -includeItem=&lt;filePattern>     ; Include files or dir by regex match
   -excludeItem=&lt;filePattern>     ; Exclude files or dir by regex match
   -IncludePath=&lt;pathPattern>     ; Include path by regex match
   -ExcludePath=&lt;pathPattern>     ; Exclude path by regex match
   -range=beg,end                 ; Optional line range filter

   -regex                         ; Use regex pattern not DOS pattern
        The include/exclude patterns do an internal conversion to full regex
          *  -> .*     and ? -> .
        Ex: "*.png"   internally becomes  ".*.png"
        Use -regex before -inc/-exc options to force native regex pattern
     Example to ignore all dot directories and files:
          -regex -exclude="[.].*"

   directories...                 ; Directories or files  to scan

Other options:
   -ignoreCase                    ; Next pattern set to ignore case
   -force                         ; Allow updates on read-only files
   -no                            ; Dry run, show changes if replacing
   -inverse                       ; Invert Search, show files not matching
   -maxFileSize=&lt;#MB>             ; Max file size MB, def= 200
   -maxLineSize=320               ; Max line shown using -from only
   -binary                        ; Include binary files
   -quiet                         ; Do not show matches
   -line                          ; Force line-by-line compare, def: entire file
   -threads                       ; Search/Replace using 20 threads
   -threads=&lt;#threads>            ; Search/Replace using threads

PrintFmt:
   -printFmt=' %r\%f(%o) %l\n'    ; Printf format to present match
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
  llreplace -from='Copyright' -include=*.java -print='%r/%f\n' src1 src2
  llreplace -from='Copyright' -include=*.java -include=*.xml -print='%s' -inverse src res
  llreplace '-from=if [(]MapConfigInfo.DEBUG[)] [{][\r\n ]*Log[.](d|e)([(][^)]*[)];)[\r\n ]*[}]'  '-include=*.java' -range=0,10 -range=20,-1 -printFmt='%f %03d: ' src1 src2
  llreplace -printFmt="%m\n" -from="&lt;section id='trail-stats'>((?!&lt;/section).|\r|\n)*&lt;/section>"

  output option can be used with search to save matches. Default is to console
  llreplace -out=matches.txt  -from=Copyright dir1 dir2 file1 file2

 Search and replace in-place:
  llreplace '-from=if [(]MapConfigInfo.DEBUG[)] [{][\r\n ]*Log[.](d|e)([(][^)]*[)];)[\r\n ]*[}]' '-to=MapConfigInfo.$1$2$3' '-include=*.java' src
  llreplace '-from=&lt;block>' -till='&lt;/block>' '-to=' '-include=*.xml' res
   llreplace -from="http:" -to="https:" -Exc=*\\.git  .
   llreplace -from="http:" -to="https:" -Exc=*\\.(git||vs) .
   llreplace -from="http:" -to="https:" -regex -Exc=.*\\[.](git||vs) .
   llreplace -from="http:" -to="https:" -regex -exc="[.](git||vs)" .

</pre>

### License

```
Copyright 2026 Dennis Lang

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
See [LICENSE.txt](LICENSE.txt) for the full license text.

Visit home website

[https://landenlabs.com](https://landenlabs.com)
