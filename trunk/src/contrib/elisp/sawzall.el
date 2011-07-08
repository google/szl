;;; sawzall.el -- a major mode for editing sawzall programs
;;
;; Copyright 2011 Google Inc. All Rights Reserved.
;;
;; By Greg J. Badros, October 2003
;;    David Hilley, September 2010
;;

;;; License:
;;
;; Licensed under the Apache License, Version 2.0 (the "License");
;; you may not use this file except in compliance with the License.
;; You may obtain a copy of the License at
;;
;;      http://www.apache.org/licenses/LICENSE-2.0
;;
;; Unless required by applicable law or agreed to in writing, software
;; distributed under the License is distributed on an "AS IS" BASIS,
;; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
;; See the License for the specific language governing permissions and
;; limitations under the License.

;;; Commentary:
;;
;; This is just getting started, but already is useful for
;; syntax-highlighting when reading code.
;; The indentation is still not even close to ideal, but
;; at least it uses cc-mode so it might be possible to
;; configure those settings without having to write a wholly
;; new ident-command.
;;
;; Emacs pointers:
;; http://two-wugs.net/emacs/mode-tutorial.html
;; Glickstein's _Writing_GNU_Emacs_Extensions_
;; Modes such as cc-mode.el, cc-lang.el, pascal-mode.el, etc.
;; C# mode built on-top of cc-mode: http://davh.dk/script/csharp-mode.el


(require 'cc-mode)
(require 'font-lock)

(defgroup sawzall nil
  "Major mode for editing Sawzall programs"
  :group 'languages)

(defvar sawzall-mode-version "0.2"
  "Version of `sawzall.el'.")

(defvar sawzall-mode-abbrev-table nil
  "Abbrev table for use in sawzall-mode buffers.")

(define-abbrev-table 'sawzall-mode-abbrev-table ())

(defvar sawzall-mode-map nil
  "Keymap for sawzall (szl) major mode")

(if sawzall-mode-map
    nil
  (setq sawzall-mode-map (c-make-inherited-keymap))
  )

(defvar sawzall-mode-syntax-table nil
  "Syntax table in use in sawzall-mode buffers.")

(if sawzall-mode-syntax-table
    nil
  (setq sawzall-mode-syntax-table (make-syntax-table))
  ;; much of it is similar to C
  (c-populate-syntax-table sawzall-mode-syntax-table)
  (modify-syntax-entry ?_ "w" sawzall-mode-syntax-table)
  (modify-syntax-entry ?# "<" sawzall-mode-syntax-table)
  (modify-syntax-entry ?\n ">" sawzall-mode-syntax-table)
  (modify-syntax-entry ?/ "@" sawzall-mode-syntax-table)
  (modify-syntax-entry ?* "@" sawzall-mode-syntax-table)
  )

(defvar sawzall-basic-offset 2
  "The basic indentation offset.")

;;; This probably shouldn't be done in here, but should be a part
;;; of the instructions and/or an sawzall-insinuate-auto-mode-alist function
(add-to-list 'auto-mode-alist '("\\.szl$" . sawzall-mode))

(defconst sawzall-symbol-regexp "\\<[a-zA-Z_][a-zA-Z_0-9]*\\>")

;; Regenerate this list with:
;; $ cat scanner.cc | grep '  DEF' | grep -v SuperSawzall | \
;; egrep -o '"[a-z]+"' | sort | xargs -d'\n' | fold -s -w70
;; # re-run removing -v from grep for SuperSawzall keywords
;; $ cat sawzall.cc | grep '::RegisterTableType' | \
;; egrep -o '"[a-zA-Z]+"' | sort | xargs -d'\n' | fold -s -w70
(defconst sawzall-keyword-regexp
  (concat
   "\\<"
   (regexp-opt
    (append
     ;; Sawzall keywords
     '("all" "and" "array" "break" "case" "continue" "default" "do" "each"
       "else" "emit" "export" "file" "for" "format" "function" "if" "import"
       "include" "map" "mill" "millmerge" "not" "of" "or" "parsedmessage" "proc"
       "proto" "rest" "return" "skip" "some" "static" "submatch" "switch"
       "table" "type" "weight" "when" "while")
     ;; SuperSawzall keywords
     ;; TODO: consider a SuperSawzall derived mode.
     '("includeinjobs" "job" "keyby" "merge" "pipeline")
     ;; Table "kinds"
     '("bootstrapsum" "collection" "distinctsample" "inversehistogram"
       "maximum" "minimum" "mrcounter" "quantile" "recordio" "sample" "set"
       "sum" "text" "top" "unique" "weightedsample"))
    t)
   "\\>")
  "Sawzall keywords from scanner.cc and table kinds from szlutils.cc")


(defconst sawzall-primitive-type-regexp
  (concat "\\<"
          (regexp-opt '("int" "uint" "bool" "float" "string"
                        "time" "bytes" "fingerprint" "void") t)
          "\\>")
  "Sawzall primitive types, from type.cc")


;; Regenerate this giant list with:
;; $ szl --explain= | cat - (extras_file) | xargs -n1 | tail -n+9 | \
;; sort | sed -e 's/^/"/' -e 's/$/"/' | xargs -d'\n' | fold -s -w70
;;
;; # change (N) in tail -n+(N) to match the number of primitive types
;; # (extras_file) should contain any miscellaneous extras
;; TODO: do this automatically at startup (in an async process)
(defconst sawzall-intrinsic-function-regexp
  (concat
   "\\<"
   (regexp-opt
    '("DEBUG" "HOUR" "HR" "Inf" "MIN" "MINUTE" "NaN" "PI" "SEC" "SECOND"
      "SQL_DB" "___addressof" "___heapcheck" "___raise_segv" "___undefine"
      "_undef_cnt" "_undef_details" "abs" "acos" "acosh" "addday"
      "addmonth" "addweek" "addyear" "asin" "asinh" "assert" "atan" "atan2"
      "atanh" "bytesfind" "bytesrfind" "ceil" "clearproto" "convert" "cos"
      "cosh" "dayofmonth" "dayofweek" "dayofyear" "dbconnect" "dbquery"
      "def" "exp" "fabs" "false" "fingerprintof" "floor" "format"
      "formattime" "frombase64" "getadditionalinput" "getenv"
      "getresourcestats" "gunzip" "gzip" "haskey" "highbit" "hourof" "inf"
      "inproto" "inprotocount" "isfinite" "isinf" "isnan" "isnormal" "keys"
      "len" "ln" "load" "lockadditionalinput" "log10" "lookup" "lowercase"
      "match" "matchposns" "matchstrs" "max" "min" "minuteof" "monthof"
      "nan" "new" "now" "nrand" "output" "pow" "rand" "regex"
      "resourcestats" "round" "saw" "sawn" "sawzall" "secondof"
      "setadditionalinput" "sin" "sinh" "sort" "sortx" "splitcsv"
      "splitcsvline" "sqrt" "stderr" "stdout" "strfind" "strreplace"
      "strrfind" "tan" "tanh" "tobase64" "true" "trunc" "trunctoday"
      "trunctohour" "trunctominute" "trunctomonth" "trunctosecond")
    t)
   "\\>")
  "Sawzall predeclared identifiers (minus basic types) from 'szl --explain'")


(defconst sawzall-font-lock-keywords
  (list
   (list sawzall-keyword-regexp
         '(1 font-lock-keyword-face))
   ;; part of the emit syntax
   '("\\(<-\\)" (1 font-lock-keyword-face))
   (list sawzall-primitive-type-regexp
         '(1 font-lock-type-face))
   ;; part of the SuperSawzall job syntax
   '("\\(->\\)" (1 font-lock-keyword-face))
   (list sawzall-primitive-type-regexp
         '(1 font-lock-type-face))
   ;; these are from scanner.cc's Scanner::Scan*() IdentMatches calls
   '("\\<\\(proto\\|import\\|include\\|includeinjobs\\)\\>"
     (1 font-lock-reference-face))
   ;; function definitions
   (list (concat "\\(" sawzall-symbol-regexp "\\)"
                 "\\s-*:\\s-*=?\\s-*\\(function\\)\\s-*(")
         '(1 font-lock-function-name-face)
         '(2 font-lock-keyword-face))
   ;; functions declared with a function type
   (list (concat "\\(" sawzall-symbol-regexp "\\)\\s-*:\\s-*"
                 sawzall-symbol-regexp "[ \t\n]*{")
         '(1 font-lock-function-name-face))
   ;; lots of built-in functions
   (list sawzall-intrinsic-function-regexp
         '(1 font-lock-variable-name-face))
   ;; Google naming convention for constants.
   '("\\<\\(k[A-Z][A-Za-z0-9_]+\\)\\>"
     (1 font-lock-constant-face))
   ))

;; XEmacs 21.4.12 gets a `font-lock-pre-idle-hook' error when we use
;; the keywords above, but not with these below, which are from an
;; older version of sawzall.el.  It's due to an XEmacs bug that's
;; fixed on some versions, but not in any reliable way.
;;
;; NOTE: to regenerate these keyword regexps from the above
;; sawzall-keyword-regexp defconst, take each list "lst" and evaluate:
;; (concat "\\<\\(" (mapconcat 'regexp-quote lst "\\|") "\\)\\>")
;;
;; TODO: re-validate XEmacs behavior and consider future support.
(and (boundp 'xemacsp)
     xemacsp
     (setq sawzall-font-lock-keywords
           (list
            ;; from spec
            '("\\(#.*\\)"
              1 font-lock-comment-face)
            ;; Sawzall keywords from scanner.cc
            '("\\<\\(all\\|and\\|array\\|break\\|case\\|continue\\|default\\|do\\|each\\|else\\|emit\\|export\\|file\\|for\\|format\\|function\\|if\\|import\\|include\\|map\\|mill\\|millmerge\\|not\\|of\\|or\\|parsedmessage\\|proc\\|proto\\|rest\\|return\\|skip\\|some\\|static\\|submatch\\|switch\\|table\\|type\\|weight\\|when\\|while\\)\\>"
              1 font-lock-keyword-face)
            ;; SuperSawzall keywords from scanner.cc
            '("\\<\\(includeinjobs\\|job\\|keyby\\|merge\\|pipeline\\)\\>"
              1 font-lock-keyword-face)
            ;; Table kinds from sawzall.cc
            '("\\<\\(bootstrapsum\\|collection\\|distinctsample\\||inversehistogram\\|maximum\\|minimum\\|mrcounter\\|quantile\\|recordio\\|sample\\|set\\|sum\\|text\\|top\\|unique\\|weightedsample\\)\\>"
              1 font-lock-keyword-face)
            ;; part of the emit syntax
            '("\\(<-\\)"
              1 font-lock-keyword-face)
            ;; part of the SuperSawzall job syntax
            '("\\(->\\)"
              1 font-lock-keyword-face)
            ;; these are from type.cc
            '("\\<\\(int\\|uint\\|bool\\|float\\|string\\|time\\|bytes\\|fingerprint\\|void\\)\\>"
              1 font-lock-type-face)
            ;; these are from scanner.cc's Scanner::Scan*() IdentMatches calls
            '("\\<\\(proto\\|import\\|include\\|includeinjobs\\)\\>"
              1 font-lock-reference-face)
            )))

(put 'sawzall-mode 'font-lock-defaults '(sawzall-font-lock-keywords nil t))

;; Wrapper function needed for Emacs 21 and XEmacs (Emacs 22 offers the more
;; elegant solution of composing a list of lineup functions or quantities with
;; operators such as "add")
(defun google-c-lineup-expression-plus-4 (langelem)
  "Indents to the beginning of the current C expression plus 4 spaces.

This implements title \"Function Declarations and Definitions\" of the Google
C++ Style Guide for the case where the previous line ends with an open
parenthese.

\"Current C expression\", as per the Google Style Guide and as
clarified by subsequent discussions means the whole expression
regardless of the number of nested parentheses, but excluding
non-expression material such as \"if(\" and \"for(\" control
structures.

Suitable for inclusion in `c-offsets-alist'."
  (save-excursion
    (back-to-indentation)
    ;; Go to beginning of *previous* line:
    (c-backward-syntactic-ws)
    (back-to-indentation)
    ;; We are making a reasonable assumption that if there is a control
    ;; structure to indent past, it has to be at the beginning of the line.
    (if (looking-at "\\(\\(if\\|for\\|while\\)\\s *(\\)")
        (goto-char (match-end 1)))
    (vector (+ 4 (current-column)))))

(defconst google-c-style
  `((c-recognize-knr-p . nil)
    (c-enable-xemacs-performance-kludge-p . t) ; speed up indentation in XEmacs
    (c-basic-offset . 2)
    (indent-tabs-mode . nil)
    (c-comment-only-line-offset . 0)
    (c-hanging-braces-alist . (
                               (defun-open after)
                               (defun-close before after)
                               (class-open after)
                               (class-close before after)
                               (namespace-open after)
                               (inline-open after)
                               (inline-close before after)
                               (block-open after)
                               (block-close . c-snug-do-while)
                               (extern-lang-open after)
                               (extern-lang-close after)
                               (statement-case-open after)
                               (substatement-open after)
                               ))
    (c-hanging-colons-alist . (
                               (case-label)
                               (label after)
                               (access-label after)
                               (member-init-intro before)
                               (inher-intro)
                               ))
    (c-hanging-semi&comma-criteria
     . (c-semi&comma-no-newlines-for-oneline-inliners
        c-semi&comma-inside-parenlist
        c-semi&comma-no-newlines-before-nonblanks))
    (c-indent-comments-syntactically-p . t)
    (comment-column . 40)
    (c-indent-comment-alist . ((other . (space . 2))))
    (c-cleanup-list . (brace-else-brace
                       brace-elseif-brace
                       brace-catch-brace
                       empty-defun-braces
                       defun-close-semi
                       list-close-comma
                       scope-operator))
    (c-offsets-alist . (
                        (arglist-intro google-c-lineup-expression-plus-4)
                        (func-decl-cont . ++)
                        (member-init-intro . ++)
                        (inher-intro . ++)
                        (comment-intro . 0)
                        (arglist-close . c-lineup-arglist)
                        (topmost-intro . 0)
                        (block-open . 0)
                        (inline-open . 0)
                        (substatement-open . 0)
                        (statement-cont
                         .
                         (,(when (fboundp 'c-no-indent-after-java-annotations)
                             'c-no-indent-after-java-annotations)
                          ,(when (fboundp 'c-lineup-assignments)
                             'c-lineup-assignments)
                          ++))
                        (label . /)
                        (case-label . +)
                        (statement-case-open . +)
                        (statement-case-intro . +) ; case w/o {
                        (access-label . /)
                        (innamespace . 0)
                        ))
    )
  "Google C/C++ Programming Style")

(c-add-style "google" google-c-style)

;; Sawzall mode formatting is derived from Google C/C++ style.
(c-add-style "sawzall"
             `("google"
                (c-basic-offset . ,sawzall-basic-offset)
                (c-comment-only-line-offset . nil)
                (c-block-comment-prefix . "#")
                (c-comment-prefix-regexp . "#")))


;; Must use `define-derived-mode' to make it (also) compatible with emacs22.
;; Unfortunately it's not compatible with XEmacs; it triggers a known bug
;; in XEmacs with some function run by the font-lock-pre-idle-hook.  I've
;; tried debugging it and various other workarounds, but using the macro
;; define-derived-mode (even with an empty body) causes problems every time.
;; So I'm using the old definition (which doesn't work with emacs22) for Xemacs.
;; Note that this is a second workaround, in addition to the one above.
;; -stevey 7/7/2006

(if (and (boundp 'xemacsp) xemacsp)

    ;; xemacs...
    (defun sawzall-mode ()
      "Major mode for editing Sawzall code."
      (interactive)
      (c-initialize-cc-mode)
      (kill-all-local-variables)
      (use-local-map sawzall-mode-map)
      (setq major-mode 'sawzall-mode)
      (setq mode-name "sawzall")
      (setq local-abbrev-table sawzall-mode-abbrev-table)
      (set-syntax-table sawzall-mode-syntax-table)
      (if (not (assq 'sawzall-mode c-default-style))
          (setq c-default-style (cons '(sawzall-mode . "sawzall") c-default-style)))
      (c-common-init)
      (make-local-variable 'comment-start)
      (make-local-variable 'comment-end)
      (make-local-variable 'c-label-key)
      (make-local-variable 'c-pound-literal)
      (make-local-variable 'font-lock-defaults)
      (make-local-variable 'c-block-comment-prefix)
      (setq comment-start "#")
      (setq comment-end "")
      (setq c-comment-start-regexp "#")
      (setq c-label-key "somenonsenseregexpthatwillnotmatch")
      (setq c-pound-literal 'bogus-pound)
      (setq font-lock-defaults '(sawzall-font-lock-keywords))
      ;; (make-local-variable 'compilation-error-regexp-alist)
      ;; (setq compilation-error-regexp-alist ...)
      (run-hooks 'c-mode-common-hook)
      (setq c-block-comment-prefix "# ")
      (run-hooks 'sawzall-mode-hook)
      (c-update-modeline))

  ;; else...
  (define-derived-mode sawzall-mode c-mode "sawzall"
    "Major mode for editing Sawzall code.
Turning on sawzall-mode runs `sawzall-mode-hook'."
    (progn
      (use-local-map sawzall-mode-map)
      (setq local-abbrev-table sawzall-mode-abbrev-table)
      (set-syntax-table sawzall-mode-syntax-table)
      (if (not (assq 'sawzall-mode c-default-style))
          (setq c-default-style
                (cons '(sawzall-mode . "sawzall") c-default-style)))
      (set (make-local-variable 'comment-start) "# ")
      (set (make-local-variable 'comment-end) "")
      (set (make-local-variable 'comment-start-skip) "#+ *")
      ; Turn off recognition of colons as labels (emacs-22 and beyond)
      (set (make-local-variable 'c-recognize-colon-labels) nil)
      ; Turn off recognition of colons as labels (emacs-21 and before)
      (set (make-local-variable 'c-label-key) "somenonsenseregexpthatwillnotmatch")
      ;?? (set (make-local-variable 'c-pound-literal) 'bogus-pound)
      (setq c-block-comment-prefix "# ")
      (c-set-style "sawzall")
      (setq font-lock-defaults
            `(sawzall-font-lock-keywords
              nil  ; keywords-only
              nil  ; case-fold
              nil  ; syntax-alist
              nil  ; syntax-regexp
              (font-lock-fontify-region-function . ,(if (>= emacs-major-version 22)
                                                        'sawzall-fontify-region
                                                      'font-lock-default-fontify-region))
              ;; Need this for syntax-table text property to work.
              (parse-sexp-lookup-properties . t))))))

(defun sawzall-fontify-region (beg end loudly)
  "Mark backquoted strings for the syntactic fontifier.

Emacs' font-lock mode offers a dizzying array of options for customizing
fontification, most of which don't actually work for Sawzall strings.
The problem is that Emacs thinks that if you tell it something is a string,
then normal escapes are active inside the string.

We can work around it by setting `font-lock-fontify-region-function' to
this function.  We scan the region for backquoted strings, mark the
backquotes as string delimiters, and then mark any backslash (`\') chars
as NOT having escape syntax.  We then tell the Emacs string/comment/sexp
scanner to honor our settings with `parse-sexp-lookup-properties', which
we set using `font-lock-defaults' in sawzall-mode.

Note that this function, apparently like all (?) font-lock functions,
doesn't break into the debugger if you instrument it with `edebug-defun'.
It only enters the debugger if you've instrumented it _and_ there was
an error signaled in the function body, which only gives you backtrace
information.  So you have to use printf-debugging, by inserting calls
to `message' and examining the *Messages* buffer after each invocation.

I haven't bothered to make this work for XEmacs.  XEmacs is buggy and
broken.  You shouldn't be using it anymore.  --stevey@google.com

The fix also doesn't work for emacs21 or earlier."
  (let ((start (save-excursion
                 (goto-char beg)
                 (point-at-bol)))
        (finish (save-excursion
                  (goto-char end)
                  (point-at-eol)))
        state
        found-first)
    (save-excursion
      (remove-text-properties start finish '(syntax-table))
      (goto-char start)
      ;; find first backquote in buffer not in a string or comment
      (while (search-forward "`" finish t)
        (when (not found-first)
          (setq state (parse-partial-sexp 1 (point)))
          (setq found-first (not (or (nth 3 state)     ; in C-string
                                     (nth 4 state))))) ; in comment
        (when found-first
          (let ((string-start (point)))
            ;; Look for matching close-quote.
            (when (search-forward "`" (point-at-eol) t)
              ;; Mark open- and close-quotes with string-quotes syntax.
              (put-text-property (1- string-start) string-start
                                 'syntax-table
                                 '(7 . ?`))  ; (string-quote . matching-char)
              (put-text-property (1- (point)) (point)
                                 'syntax-table
                                 '(7 . ?`))
              ;; Find all `\' chars in the string and give them word-syntax,
              ;; so that font-lock will treat \` as terminating the string.
              (save-excursion
                (save-restriction
                  (narrow-to-region string-start (1- (point)))
                  (goto-char (point-min))
                  (while (search-forward "\\" nil t)
                    (put-text-property (1- (point)) (point)
                                       'syntax-table
                                       '(2))))))))))  ; word-syntax
    ;; Now we can invoke the default fontification function.
    (font-lock-default-fontify-region beg end loudly)))

;; unlike c-mode, pounds in szl are comments, not macro directives.
(defadvice c-beginning-of-macro (around disable-cpp-macro-in-sawzall activate)
  "Disable recognition of cpp macros in sawzall code"
  (unless (derived-mode-p 'sawzall-mode)
    ad-do-it))

;; Taken from cc-mode's c-show-syntatic-information; returns the
;; current line's guessed syntax.
(defun szl-get-c-syntax ()
  "Return cc-mode's syntactic information for current line."
  (let* ((c-parsing-error nil)
         (syntax (c-save-buffer-state nil (c-guess-basic-syntax))))
    (when (not c-parsing-error)
      syntax)))

(defun szl-find-func-rettype (init-point sym)
  "Determine if a Sawzall function declares a return type.

Attempts to find functions with a return type ': (type) {'.
Locates the closing paren and returns whether or not the opening
brace is on the same line.  Return value is a list:
(<position of param list closing paren> <newline-before>) or nil
if is not found.  NOTE: This function moves point, so it should
be invoked within `save-excursion'."
  (let* ((orig-point (point))
         (ret-type-1 (progn (goto-char (c-point 'eol))
                            ;; Find the end of the function parameters
                            (re-search-backward "):[^{]*{" nil t)))
         (ret-type-2 (progn (goto-char orig-point)
                            ;; Try again in case the current line contains
                            ;; a nested definition
                            (goto-char (c-point 'bol))
                            (re-search-backward "):[^{]*{" init-point t)))
         (ret-type (if (and ret-type-1 ret-type-2)
                       (min ret-type-1 ret-type-2)
                     (or ret-type-1 ret-type-2)))
         (nl-before-pos (when ret-type
                          (goto-char ret-type)
                          (re-search-forward ":[^{\n]*{" nil t)))
         (nl-before (and nl-before-pos
                          (equal (match-beginning 0) (+ ret-type 1)))))
    (when (and ret-type
               (<= init-point ret-type))
      (list ret-type nl-before))))

(defun szl-has-split-param-list (ret-type-point limit)
  "Returns whether a function's parameters are split across lines."
  (let* ((sp (progn (goto-char ret-type-point)
                    ;; Find the start of the function parameters
                    (re-search-backward "(" limit t)))
         (sp-eol (when sp
                   (goto-char sp)
                   (point-at-eol)))
         (ep-eol (progn
                   (goto-char ret-type-point)
                   (point-at-eol))))
    (and sp (not (= sp-eol ep-eol)))))

;; Attempts to fix the annoying indent problems when Sawzall functions
;; have return types and parameter lists are split between lines.
;;
;; Note: it would be more elegant to defadvice after
;; c-guess-basic-syntax to try to modify the way cc-mode's determines
;; the enclosing parent of lines related to function defs.  It
;; mis-recognizes the Sawzall : type syntax as gcc asm directives, so
;; we're just hacking around it here, but cc-mode is just one big hack
;; anyway -- it guesses C/C++ syntax with a ton of regexps rather than
;; parsing it.  See Steven Champeon's javascript-mode.el for an
;; example of using defadvice to modify cc-mode's default guesses.
(defun szl-fix-func-with-rettype-indent (langelem)
  "Indent hook for Sawzall to fix function indentation."
  (when (derived-mode-p 'sawzall-mode)
    (save-excursion
      (let* ((init-point (c-langelem-pos langelem))
             (sym (c-langelem-sym langelem))
             (init-syntax (szl-get-c-syntax))
             (ret-type (szl-find-func-rettype init-point sym)))
        (when (and ret-type
                   (= 1 (length init-syntax)))
          (let* ((ret-type-point (first ret-type))
                 (nl-before (not (second ret-type)))
                 (fp-syntax (progn
                              ;; Get the guessed syntax at the ')'
                              ;; terminating function params, because
                              ;; we want that starting column
                              (goto-char ret-type-point)
                              (car (szl-get-c-syntax))))
                 (fp-syntax-point (c-langelem-pos fp-syntax))
                 ;; Guess if the function was defined with new := syntax
                 ;; or old : function declaration syntax
                 (colon-eq (progn (goto-char fp-syntax-point)
                                  (re-search-forward ":=" init-point t)))
                 ;; Parents' parent indent is for cases like this:
                 ;; a: function(i: int): int
                 ;; {      # parent is i in the parameter list
                 ;;        # parent's parent is a (what we want)
                 (parents-parent-indent (vector (c-langelem-col fp-syntax t)))
                 ;; Parent's parent indent + is for cases like this:
                 ;; a: function(i: int): int {
                 ;;   stmt;   # parent's parent is a, so the +
                 ;;           # gives it the normal block indent
                 (parents-parent-indent-+
                  `(add ,(vector (c-langelem-col fp-syntax t)) +)))
            ;; Several cases here, depending on the surrounding context:
            ;; NOTE: these examples show one line parameter lists, but
            ;; we're really only touching lists split across lines
            ;;
            ;;   1.   a: function(i: int): int {
            ;;          stmt;    # 'defun-block-intro
            ;;
            ;;   2a.  a: function(i: int): int
            ;;        {   # 'defun-open
            ;;   2b.  a:= function(i: int): int
            ;;        {   # 'defun-open
            ;;
            ;;   3.   a: function(i: int): int
            ;;        {
            ;;          stmt;  # 'defun-block-intro
            ;;
            ;;   4.   a: function(i: int): array of
            ;;        int {  # 'topmost-intro-cont
            ;;
            (cond
             ;; Case 0: param list on one line; don't touch indent
             ((not (szl-has-split-param-list ret-type-point
                                             fp-syntax-point)) nil)
             ;; Case 1:
             ((and (not nl-before) (equal sym 'defun-block-intro))
              (unless colon-eq parents-parent-indent-+))
             ;; Case 2:
             ((and nl-before (equal sym 'defun-open)) parents-parent-indent)
             ;; Case 3:
             ((equal sym 'defun-block-intro) parents-parent-indent-+)
             ;; Case 4:
             ((and nl-before (equal sym 'topmost-intro-cont))
              parents-parent-indent)
             ;; Unknown; don't modify indentation
             (t nil))))))
    ))

(provide 'sawzall)
