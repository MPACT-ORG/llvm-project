! RUN: %python %S/test_errors.py %s %flang_fc1
subroutine s1()
  ! C701 (R701) The type-param-value for a kind type parameter shall be a
  ! constant expression.
  !
  ! C702 (R701) A colon shall not be used as a type-param-value except in the
  ! declaration of an entity that has the POINTER or ALLOCATABLE attribute.
  !
  ! C704 (R703) In a declaration-type-spec, every type-param-value that is
  ! not a colon or an asterisk shall be a specification expression.
  !   Section 10.1.11 defines specification expressions
  !
  ! 15.4.2.2(4)(c) A procedure must have an explicit interface if it has a
  ! result that has a nonassumed type parameter value that is not a constant
  ! expression.
  !
  integer, parameter :: constVal = 1
  integer :: nonConstVal = 1
!PORTABILITY: specification expression refers to local object 'nonconstval' (initialized and saved) [-Wsaved-local-in-spec-expr]
  character(nonConstVal) :: colonString1
  character(len=20, kind=constVal + 1) :: constKindString
  character(len=:, kind=constVal + 1), pointer :: constKindString1
!ERROR: 'constkindstring2' has a type CHARACTER(KIND=2,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  character(len=:, kind=constVal + 1) :: constKindString2
!ERROR: Must be a constant value
  character(len=20, kind=nonConstVal) :: nonConstKindString
!ERROR: 'deferredstring' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  character(len=:) :: deferredString
!ERROR: 'colonstring2' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  character(:) :: colonString2
  !OK because of the allocatable attribute
  character(:), allocatable :: colonString3
!ERROR: 'foo1' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  character(:), external :: foo1
!ERROR: 'foo2' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  procedure(character(:)) :: foo2
  interface
    function foo3()
!ERROR: 'foo3' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
      character(:) foo3
    end function
  end interface

!ERROR: Must have INTEGER type, but is REAL(4)
  character(3.5) :: badParamValue

  type derived(typeKind, typeLen)
    integer, kind :: typeKind
    integer, len :: typeLen
    character(typeKind) :: kindValue
    character(typeLen) :: lenValue
  end type derived

  type (derived(constVal, 3)) :: constDerivedKind
!ERROR: Value of KIND type parameter 'typekind' must be constant
!PORTABILITY: specification expression refers to local object 'nonconstval' (initialized and saved) [-Wsaved-local-in-spec-expr]
  type (derived(nonConstVal, 3)) :: nonConstDerivedKind

  !OK because all type-params are constants
  type (derived(3, constVal)) :: constDerivedLen

!PORTABILITY: specification expression refers to local object 'nonconstval' (initialized and saved) [-Wsaved-local-in-spec-expr]
  type (derived(3, nonConstVal)) :: nonConstDerivedLen
!ERROR: 'colonderivedlen' has a type derived(typekind=3_4,typelen=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  type (derived(3, :)) :: colonDerivedLen
!ERROR: Value of KIND type parameter 'typekind' must be constant
!ERROR: 'colonderivedlen1' has a type derived(typekind=:,typelen=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  type (derived( :, :)) :: colonDerivedLen1
  type (derived( :, :)), pointer :: colonDerivedLen2
  type (derived(4, :)), pointer :: colonDerivedLen3
end subroutine s1

!C702
!ERROR: 'f1' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
character(:) function f1
end function

function f2
!ERROR: 'f2' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  character(:) f2
end function

function f3() result(res)
!ERROR: 'res' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  character(:) res
end function

!ERROR: 'f4' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
function f4
  implicit character(:)(f)
end function

!Not errors.

Program d5
  Type string(maxlen)
    Integer,Kind :: maxlen
    Character(maxlen) :: value
  End Type
  Type(string(80)) line
  line%value = 'ok'
  Print *,Trim(line%value)
End Program

subroutine outer
  integer n
 contains
  character(n) function inner1()
    inner1 = ''
  end function inner1
  function inner2()
    real inner2(n)
  end function inner2
end subroutine outer

subroutine s2(dp,dpp)
  !ERROR: 'dp' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  procedure(character(:)) :: dp
  !ERROR: 'dpp' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  procedure(character(:)), pointer :: dpp
  !ERROR: 'pp' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  procedure(character(:)), pointer :: pp
  !ERROR: 'xp' has a type CHARACTER(KIND=1,LEN=:) with a deferred type parameter but is neither an allocatable nor an object pointer
  procedure(character(:)) :: xp
end subroutine
