package compiler

import (
	"go/types"
)

// kind classifies a type by how it is represented in zForth.
type kind int

const (
	kInvalid kind = iota
	kBool
	kI8
	kI16
	kI32
	kU8
	kU16
	kU32
	kI64
	kU64
	kF32
	kF64
	kString
	kSlice
	kArray
	kStruct
	kPointer
)

func kindOf(t types.Type) kind {
	switch u := t.Underlying().(type) {
	case *types.Basic:
		switch u.Kind() {
		case types.Bool, types.UntypedBool:
			return kBool
		case types.Int8:
			return kI8
		case types.Int16:
			return kI16
		case types.Int32, types.Int, types.UntypedInt, types.UntypedRune:
			return kI32
		case types.Uint8:
			return kU8
		case types.Uint16:
			return kU16
		case types.Uint32, types.Uint, types.Uintptr:
			return kU32
		case types.Int64:
			return kI64
		case types.Uint64:
			return kU64
		case types.Float32:
			return kF32
		case types.Float64, types.UntypedFloat:
			return kF64
		case types.String, types.UntypedString:
			return kString
		case types.UntypedNil:
			return kPointer
		}
	case *types.Slice:
		return kSlice
	case *types.Array:
		return kArray
	case *types.Struct:
		return kStruct
	case *types.Pointer:
		return kPointer
	}
	return kInvalid
}

// Integer kinds that fit in one cell.
func (k kind) isInt32() bool {
	switch k {
	case kI8, kI16, kI32, kU8, kU16, kU32:
		return true
	}
	return false
}

func (k kind) isInt64() bool { return k == kI64 || k == kU64 }
func (k kind) isInt() bool   { return k.isInt32() || k.isInt64() }
func (k kind) isFloat() bool { return k == kF32 || k == kF64 }

func (k kind) isSigned() bool {
	switch k {
	case kI8, kI16, kI32, kI64:
		return true
	}
	return false
}

// isNarrow reports whether results must be re-narrowed after arithmetic.
func (k kind) isNarrow() bool {
	switch k {
	case kI8, kI16, kU8, kU16:
		return true
	}
	return false
}

// inMemory reports whether values of this kind live in memory and are
// handled by address (arrays and structs).
func (k kind) inMemory() bool { return k == kArray || k == kStruct }

// cells is the number of stack cells a value of type t takes. Arrays and
// structs are handled by address, one cell.
func cells(t types.Type) int {
	switch kindOf(t) {
	case kI64, kU64, kF64, kString, kSlice:
		return 2
	case kInvalid:
		return 0
	}
	return 1
}

// sizeof is the size of a value of type t in memory, in bytes. Slices are
// two cells (address and length), with no capacity.
func sizeof(t types.Type) int {
	switch k := kindOf(t); k {
	case kBool, kI8, kU8:
		return 1
	case kI16, kU16:
		return 2
	case kI32, kU32, kF32, kPointer:
		return 4
	case kI64, kU64, kF64, kString, kSlice:
		return 8
	case kArray:
		a := t.Underlying().(*types.Array)
		return int(a.Len()) * sizeof(a.Elem())
	case kStruct:
		l := layoutOf(t.Underlying().(*types.Struct))
		return l.size
	}
	return 0
}

func alignof(t types.Type) int {
	switch k := kindOf(t); k {
	case kBool, kI8, kU8:
		return 1
	case kI16, kU16:
		return 2
	case kArray:
		return alignof(t.Underlying().(*types.Array).Elem())
	case kStruct:
		return layoutOf(t.Underlying().(*types.Struct)).align
	}
	return 4
}

type structLayout struct {
	offsets []int
	size    int
	align   int
}

var layouts = map[*types.Struct]*structLayout{}

// layoutOf lays out a struct's fields in order with natural alignment, at
// most 4, so C code on the device can read it.
func layoutOf(s *types.Struct) *structLayout {
	if l, ok := layouts[s]; ok {
		return l
	}
	l := &structLayout{align: 1}
	off := 0
	for i := 0; i < s.NumFields(); i++ {
		ft := s.Field(i).Type()
		a := alignof(ft)
		if a > l.align {
			l.align = a
		}
		off = (off + a - 1) / a * a
		l.offsets = append(l.offsets, off)
		off += sizeof(ft)
	}
	l.size = (off + l.align - 1) / l.align * l.align
	layouts[s] = l
	return l
}

// loadWord and storeWord are the memory words for a value of type t;
// two-cell values use df@ df!, low cell first.
func loadWord(t types.Type) string {
	switch kindOf(t) {
	case kBool, kI8:
		return "@s8"
	case kU8:
		return "@u8"
	case kI16:
		return "@s16"
	case kU16:
		return "@u16"
	case kI64, kU64, kF64, kString, kSlice:
		return "df@"
	}
	return "@c"
}

func storeWord(t types.Type) string {
	switch kindOf(t) {
	case kBool, kI8, kU8:
		return "!u8"
	case kI16, kU16:
		return "!u16"
	case kI64, kU64, kF64, kString, kSlice:
		return "df!"
	}
	return "!c"
}

// typeName is a readable name for a type, for the manifest and messages.
func typeName(t types.Type) string {
	return types.TypeString(t, func(p *types.Package) string {
		if p.Path() == "main" {
			return ""
		}
		return p.Name()
	})
}

// elemType is the element type of an array, slice, string (byte) or
// pointer to array.
func elemType(t types.Type) types.Type {
	switch u := t.Underlying().(type) {
	case *types.Array:
		return u.Elem()
	case *types.Slice:
		return u.Elem()
	case *types.Basic:
		return types.Typ[types.Byte]
	case *types.Pointer:
		if a, ok := u.Elem().Underlying().(*types.Array); ok {
			return a.Elem()
		}
	}
	return nil
}
