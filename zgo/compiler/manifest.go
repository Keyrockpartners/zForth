package compiler

import (
	"encoding/json"
	"go/types"
)

// Manifest lists the entry points the firmware can call (ZGO_PLAN.md 4.10).
// To call an export, push its parameters in order (two-cell values as
// lo hi), run its word, then pop its results (the last one on top).
// Exported package variables are listed under vars: their word pushes the
// variable's address; two-cell values are stored low cell first.
type Manifest struct {
	Init    string   `json:"init"`
	Main    string   `json:"main"`
	Exports []Export `json:"exports"`
	Vars    []Var    `json:"vars"`
}

// Var is an exported package variable.
type Var struct {
	Name string `json:"name"`
	Word string `json:"word"`
	Type string `json:"type"`
	Size int    `json:"size"`
}

// Export is an exported function.
type Export struct {
	Name    string  `json:"name"`
	Word    string  `json:"word"`
	Params  []Value `json:"params"`
	Results []Value `json:"results"`
}

// Value is a parameter or result.
type Value struct {
	Name  string `json:"name,omitempty"`
	Type  string `json:"type"`
	Cells int    `json:"cells"`
}

// JSON returns the manifest as indented JSON.
func (m *Manifest) JSON() []byte {
	b, _ := json.MarshalIndent(m, "", "  ")
	return append(b, '\n')
}

func (c *compiler) manifest() *Manifest {
	m := &Manifest{Init: "z-init", Main: "z-main", Exports: []Export{}, Vars: []Var{}}
	for _, v := range c.exportVars {
		m.Vars = append(m.Vars, Var{Name: v.Name(), Word: c.globals[v], Type: typeName(v.Type()), Size: sizeof(v.Type())})
	}
	for _, fi := range c.exports {
		sig := fi.obj.Type().(*types.Signature)
		ex := Export{Name: fi.obj.Name(), Word: fi.word, Params: []Value{}, Results: []Value{}}
		for i := 0; i < sig.Params().Len(); i++ {
			p := sig.Params().At(i)
			ex.Params = append(ex.Params, value(p))
		}
		for i := 0; i < sig.Results().Len(); i++ {
			ex.Results = append(ex.Results, value(sig.Results().At(i)))
		}
		m.Exports = append(m.Exports, ex)
	}
	return m
}

func value(v *types.Var) Value {
	n := cells(v.Type())
	if kindOf(v.Type()).inMemory() {
		n = 1 // passed by address
	}
	return Value{Name: v.Name(), Type: typeName(v.Type()), Cells: n}
}
