/***
 * https://ocsigen.org/lwt/dev/api/Ppx_lwt
 * https://github.com/zepalmer/ocaml-monadic
 */

open Migrate_parsetree;
open OCaml_403.Ast;
let fail = (loc, txt) => raise(Location.Error(Location.error(~loc, txt)));

let rec process_bindings = (bindings, ident) =>
  Parsetree.(
    switch (bindings) {
    | [] => assert(false)
    | [binding] => (binding.pvb_pat, binding.pvb_expr)
    | [binding, ...rest] =>
      let (pattern, expr) = process_bindings(rest, ident);
      (
        Ast_helper.Pat.tuple(
          ~loc=binding.pvb_loc,
          [binding.pvb_pat, pattern],
        ),
        Ast_helper.Exp.apply(
          ~loc=binding.pvb_loc,
          Ast_helper.Exp.ident(
            ~loc=binding.pvb_loc,
            Location.mkloc(Longident.Ldot(ident, "and_"), binding.pvb_loc),
          ),
          [(Asttypes.Nolabel, binding.pvb_expr), (Asttypes.Nolabel, expr)],
        ),
      );
    }
  );

let parseLongident = txt => {
  let parts = Str.split(Str.regexp_string("."), txt);
  let rec loop = (current, parts) =>
    switch (current, parts) {
    | (None, []) => assert(false)
    | (Some(c), []) => c
    | (None, [one, ...more]) => loop(Some(Longident.Lident(one)), more)
    | (Some(c), [one, ...more]) =>
      loop(Some(Longident.Ldot(c, one)), more)
    };
  loop(None, parts);
};

let mapper = (_config, _cookies) =>
  Parsetree.{
    ...Ast_mapper.default_mapper,
    /* TODO throw error on structure items */
    expr: (mapper, expr) =>
      switch (expr.pexp_desc) {
      | Pexp_extension((
          {txt, loc},
          PStr([
            {
              pstr_desc:
                Pstr_eval(
                  {pexp_loc, pexp_desc: Pexp_try(value, handlers), _},
                  _attributes,
                ),
              _,
            },
          ]),
        )) =>
        let ident = parseLongident(txt);
        let last = Longident.last(ident);
        if (last != String.capitalize_ascii(last)) {
          Ast_mapper.default_mapper.expr(mapper, expr);
        } else {
          let handlerLocStart = List.hd(handlers).pc_lhs.ppat_loc;
          let handlerLocEnd =
            List.nth(handlers, List.length(handlers) - 1).pc_rhs.pexp_loc;
          let handlerLoc = {
            ...handlerLocStart,
            loc_end: handlerLocEnd.loc_end,
          };
          let try_ =
            Ast_helper.Exp.ident(
              ~loc=pexp_loc,
              Location.mkloc(Longident.Ldot(ident, "try_"), loc),
            );
          Ast_helper.Exp.apply(
            ~loc,
            try_,
            [
              (Asttypes.Nolabel, mapper.expr(mapper, value)),
              (
                Asttypes.Nolabel,
                Ast_helper.Exp.function_(~loc=handlerLoc, handlers),
              ),
            ],
          );
        };
      | Pexp_extension((
          {txt, loc},
          PStr([
            {
              pstr_desc:
                Pstr_eval(
                  {
                    pexp_desc: Pexp_let(Nonrecursive, bindings, continuation),
                    _,
                  },
                  _attributes,
                ),
              _,
            },
          ]),
        )) =>
        let ident = parseLongident(txt);
        let last = Longident.last(ident);
        if (last != String.capitalize_ascii(last)) {
          Ast_mapper.default_mapper.expr(mapper, expr);
        } else {
          let (pat, expr) = process_bindings(bindings, ident);
          let let_ =
            Ast_helper.Exp.ident(
              ~loc,
              Location.mkloc(Longident.Ldot(ident, "let_"), loc),
            );
          Ast_helper.Exp.apply(
            ~loc,
            let_,
            [
              (Asttypes.Nolabel, mapper.expr(mapper, expr)),
              (
                Asttypes.Nolabel,
                Ast_helper.Exp.fun_(
                  ~loc,
                  Asttypes.Nolabel,
                  None,
                  pat,
                  mapper.expr(mapper, continuation),
                ),
              ),
            ],
          );
        };
      | _ => Ast_mapper.default_mapper.expr(mapper, expr)
      },
  };

/* let () = Ast_mapper.run_main(mapper); */
let () = Driver.register(~name="re-fs.ppx", (module OCaml_403), mapper);