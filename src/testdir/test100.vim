" vim: fmr=▶,▲
language C
%delete _
"▶1 Utility functions/commands
command -nargs=1 TryPut             :try|execute <q-args>|catch|$put=s:f.snrto1(v:exception)|endtry
command -nargs=1 OutPut             :redir =>messages|execute <q-args>|redir END|silent $put =messages
command -nargs=1 TestHeader         :call append('$', (empty(getline('$'))?[]:[''])+['{{{1 '.<q-args>])
command -nargs=1 ECStart            :call append('$', '{{{ Running '.<q-args>.'. This fold must be empty')
command -nargs=0 ECEnd              :call append('$', '}}}')
command -nargs=1 EmptyCommandsStart :let ecm=<q-args>|redir =>ecm_messages
command -nargs=0 EmptyCommandsEnd   :redir END|execute 'ECStart' ecm|$put =ecm_messages|execute 'ECEnd'|unlet ecm ecm_messages
command -nargs=1 EmptyCommand       :execute 'ECStart' <q-args>|execute 'TryPut OutPut' <q-args>|ECEnd
let s:f={}
function s:f.defsnr(s)
    let s:snr=matchstr(a:s, '\(<SNR>\)\@<=\d\+')
    if empty(s:snr)
        unlet s:snr
        return 0
    endif
    return 1
endfunction
function s:f.snrto1(s)
    if !exists('s:snr') && !s:f.defsnr(a:s)
        return a:s
    endif
    return substitute(a:s, '\(<SNR>\)\@<='.s:snr, 1, 'g')
endfunction
"▶1 Extended funcref test: new/old behavior
TestHeader Deleting function from inside its call
1 delete _
function T() abort
    delfunction T
    return 'Regular return'
endfunction
" Old: error out, put nothing
" New: proceed as usual: funcref is kept until call ends
"      :delfunction deletes functions only from dictionary with all user 
"      functions and also deletes dictionary keys. As with new function 
"      references there is no requirement for having function in 
"      func_hashtab to be able to call this (structure is recorded in the 
"      funcref itself) :delfunction can no longer make function references 
"      useless.
"      TODO Make delfunction able to delete global variables.
$put =T()

TestHeader Using old definition of replaced function
function T()
    return 'Old definition'
endfunction
let g:TestRef=function('T')
function! T()
    return 'New definition'
endfunction
" Old: use new definition always
" New: use new definition only for last call
"      Explanation is the same as the above: structure containing first 
"      function definition is referenced from the funcref itself.
$put =g:TestRef()
$put =T()
delfunction T
unlet g:TestRef

TestHeader Using reference to deleted function
function T()
    return 'Regular return'
endfunction
let g:TestRef=function('T')
delfunction T
" Old: error out
" New: use deleted function
"      Same.
$put =g:TestRef()
unlet g:TestRef

TestHeader Anonymous function representation
let d={}
function d.test()
endfunction
function d.test1()
endfunction
" Old: function('1') and function('2')
" New: function('1') always: anonymous functions do not get unique names
"      Neither anonymous functions are saved into func_hashtab. They are 
"      anonymous after all.
"      TODO record anonymous function names as they were defined (d.test and 
"           d.test1 for this case) for error messages. For compatibility 
"           recorded names will NOT be used by string().
"      Note: call(1, [], {}) can no longer be used.
"      Note2: eval(string(d.test)) did not work previously and continues to 
"             fail now. Nothing was changed in this case, though reasons for 
"             inability to use eval(string()) are different.
$put =string(d.test)
$put =string(d.test1)
unlet d

TestHeader Function names, including anonymous functions
let d={}
function d.F()
    throw 2233
endfunction
function Fu()
    throw 222
endfunction
function g:Fu()
    throw 444
endfunction
let d.F2=function('Fu')
redir @a
    " Old: Print “function 1() dict” and function code
    " New: Print “function d.F() dict” and function code
    silent function d.F
    " Just testing that the below works as expected. Should not see any changes
    silent function d.F2
    delfunction Fu
    silent function d.F2
    silent function g:Fu
redir END
silent $put a
TryPut call d.F()
" Same as above test for working function with reference to it
TryPut call d.F2()
unlet d
" Test that g:Fu did not define variable Fu in global dictionary
$put =string(sort(keys(g:)))
delfunction g:Fu

TestHeader Deleting anonymous functions
let d={}
function d.F()
endfunction
function Fu2()
endfunction
let g:F=d.F
let g:F2=function('Fu2')
redir @a
    delfunction g:F2
redir END
let @a=substitute(@a, '\C\v%(line\s+)@<=\d+', '\="N-".(expand("<slnum>")-submatch(0))', '')
silent $put a
" Old: bug, internal error (it deleted d.F from func_hashtab)
" New: no bug, unknown function (anonymous functions are not recorded in 
"      func_hashtab)
TryPut delfunction g:F
" No changes: no functions are defined
EmptyCommand function
unlet d
"▶1 Regressions
TestHeader Funcref comparison, string(function('s:Func'))
" string(Fref) should return function('<SNR>NN_Func') for s: functions
function s:Func()
    return 3
endfunction
call s:f.defsnr(string(function('s:Func')))
let Fref1=function('s:Func')
let Fref2=function('<SID>Func')
let Fref3=function('<SNR>'.s:snr.'_Func')
$put ='Fref1: '.s:f.snrto1(string(Fref1))
$put ='Fref2: '.s:f.snrto1(string(Fref2))
$put ='Fref3: '.s:f.snrto1(string(Fref3))
$put ='Fref1<>Fref2: '.(Fref1 is Fref2).(Fref2 is Fref1)
$put ='Fref2<>Fref3: '.(Fref2 is Fref3).(Fref3 is Fref2)
$put ='Fref1<>Fref3: '.(Fref1 is Fref3).(Fref3 is Fref1)
$put ='Fref1==Fref2: '.(Fref1 == Fref2).(Fref2 == Fref1)
$put ='Fref1!=Fref2: '.(Fref1 != Fref2).(Fref2 != Fref1)
$put ='Fref(): '.Fref1().Fref2().Fref3()
$put ='eval(string(Fref))(): '.eval(string(Fref1))().eval(string(Fref2))().eval(string(Fref3))()
$put ='eval(string(Fref).''()''): '.eval(string(Fref1).'()').eval(string(Fref2).'()').eval(string(Fref3).'()')
execute 'nnoremap <silent><special> \put :$put =''Put from nnoremap: ''.'.string(Fref1).'()<CR>'
normal \put
nunmap \put
unlet Fref1
unlet Fref2
unlet Fref3
redir => messages
    silent function s:Func
redir END
silent $put =s:f.snrto1(messages)
EmptyCommand delfunction s:Func

TestHeader Self-referencing variables
let s:F={}
let s:deplen={'def': 2}
let s:dependents={'abc': {'def': 2}, 'def': {}}
function s:F.updatedeplen(plid, newval, updated)
    $put ='Called updatedeplen: '.string(a:plid).', '.string(a:newval).', '.string(a:updated)
    let s:deplen[a:plid]=a:newval
    let a:updated[a:plid]=1
    if has_key(s:dependents, a:plid)
        let nv=a:newval+1
        call map(keys(s:dependents[a:plid]), '((!has_key(a:updated, v:val) && s:deplen[v:val]<'.nv.')?s:F.updatedeplen(v:val, '.nv.', a:updated):0)')
    endif
endfunction
$put =string(s:deplen)
call s:F.updatedeplen('abc', 1, {})
$put =string(s:deplen)
unlet s:deplen s:F s:dependents

TestHeader exists('*...')
" exists() should work with all kinds of arguments
let d={}
function s:funcexists(arg)
    return exists('*a:arg')
endfunction
function d.F()
endfunction
let d.l=[]
let d.s=''
let d.n=0
let d.d={}
$put ='funcexists(tr):'.s:funcexists(function('tr'))
$put ='funcexists(s:funcexists):'.s:funcexists(function('s:funcexists'))
$put ='funcexists(d.F):'.s:funcexists(d.F)
EmptyCommandsStart puts
    $put ='exists(''*d.n''):'.exists('*d.n')
    $put ='exists(''*d.s''):'.exists('*d.s')
    $put ='exists(''*d.F''):'.exists('*d.F')
    $put ='exists(''*d.l''):'.exists('*d.l')
    $put ='exists(''*d.d''):'.exists('*d.d')
EmptyCommandsEnd
silent $put a
delfunction s:funcexists
unlet d

TestHeader sort(, Fref)
" sort() should accept function names and function references
function s:cmp(a1, a2)
    let a1=str2nr(a:a1[-1:])
    let a2=str2nr(a:a2[-1:])
    return a1<a2 ? 1 : (a1>a2 ? -1 : 0)
endfunction
EmptyCommandsStart sort
    $put =string(sort([44, 35, 1, 60, 56, 23], 's:cmp'))
    $put =string(sort([44, 35, 1, 60, 56, 23, 18], function('s:cmp')))
    $put =s:f.snrto1(string(function('s:cmp')))
EmptyCommandsEnd
delfunction s:cmp

TestHeader function('filename#funcname'), :func AuFref
" function('filename#funcname') should not cause file loading
" :function on such function reference should return function body once the 
" function was loaded
set rtp=./test99
redir => messages
    silent scriptnames
redir END
let s:F=function('test#function')
redir => messages2
    silent scriptnames
redir END
$put ='Difference between :scriptnames lengths: '.(len(messages)-len(messages2))
unlet messages messages2
TryPut function s:F
$put ='s:F(): '.s:F()
try
    redir => messages
        silent function s:F
    redir END
catch
    $put =v:exception
endtry
silent $put =messages
unlet messages
unlet s:F

TestHeader Autoloading unexisting function
" It should not fall into infinite recursion in case requested to autoload 
" an non-existing function
let s:F=function('test2#unknown')
TryPut call s:F()
unlet s:F

TestHeader Illegal function names
" function() should not allow illegal names even if they contain #
TryPut let s:F=function('<>#<>')
TryPut let s:F=function('g:1file#function')
TryPut let s:F=function('#file#function')
" g:file#function is legal name
$put =string(function('g:file#varname'))
"▲1
unlet s:f
delcommand TryPut
delcommand OutPut
delcommand TestHeader
delcommand ECStart
delcommand ECEnd
delcommand EmptyCommandsStart
delcommand EmptyCommandsEnd
delcommand EmptyCommand
call garbagecollect(1)
