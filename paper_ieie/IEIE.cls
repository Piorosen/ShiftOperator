\ProvidesClass{kcc}[2010/12/01 v1 thesis class for SNU CSE]

\newif\if@preprint

% load default class
\DeclareOption{onecolumn}{\OptionNotUsed}
\DeclareOption{preprint}{\@preprinttrue}
\DeclareOption*{\PassOptionsToClass{\CurrentOption}{article}}
\ProcessOptions\relax

\LoadClass[10pt,a4paper,twocolumn]{article}

% margin
\usepackage[top=3.4cm, left=1.5cm, right=1.5cm, bottom=3.4cm]{geometry}

% font
\usepackage{times}
\usepackage{dhucs-cmap}
\usepackage[hangul]{kotex}

% spacing
\usepackage[hangul]{dhucs-setspace}
\SetHangulspace{1.3}{1.0}

% indent
\usepackage{indentfirst}
\setlength{\parindent}{1em}
\setlength{\parskip}{0pt}
\setlength{\columnsep}{30pt}

% footnote
\usepackage[parenthesis,hang]{dhucsfn}

% section style
\usepackage[ensec]{dhucs-sectsty}
\sectionfont{\fontsize{12pt}{12pt}\selectfont}
\subsectionfont{\fontsize{10pt}{10pt}\bfseries\selectfont}
\subsubsectionfont{\fontsize{9pt}{9pt}\selectfont}

% page number
\pagestyle{empty}

% user-defined commands
\def\engtitle#1{\gdef\@engtitle{#1}}
\def\engauthor#1{\gdef\@engauthor{#1}}
\def\abstract#1{\gdef\@abstract{#1}}

% maketitle formatting
\renewcommand{\maketitle}{
  \twocolumn[
    \begin{center}
      \vspace*{3mm}
      {\fontsize{18pt}{21.6pt}\selectfont \@title \par}
      \vspace{12mm}
      {\fontsize{10pt}{14pt}\selectfont \@author \par}
      \vspace{5mm}
      {\fontsize{12pt}{14.4pt}\selectfont \@engtitle \par}
      \vspace{8mm}
      {\fontsize{10pt}{12pt}\selectfont \@engauthor \par}
    \end{center}
  ]
}

% abstract 출력용 명령 정의 (본문 상단에 왼쪽 정렬로)
\newcommand{\makeabstract}{
  \noindent
  {\centering
   ~\\
   \fontsize{12pt}{18pt}\selectfont Abstract\par}
  \vspace{6mm}

  \fontsize{9pt}{12pt}\selectfont
  \indent\@abstract\par
  \vskip 0pt plus 0pt
}

% Section: 가운데 정렬, 볼드 없음
\renewcommand\section{\@startsection {section}{1}{\z@}%
  {-2ex \@plus -0.5ex \@minus -.2ex}%
  {2.3ex \@plus.2ex}%
  {\centering\normalfont\Large}}

% Subsection: 왼쪽 정렬, 볼드 없음
\renewcommand\subsection{\@startsection {subsection}{2}{\z@}%
  {-2.5ex \@plus -1ex \@minus -.2ex}% 위 간격 (조금 줄임)
  {0.5ex \@plus .1ex}% 아래 간격 (확실히 줄임)
  {\normalfont\large}}% 스타일

% section numbering with Roman numerals
\makeatletter
\renewcommand\thesection{\Roman{section}.}
\renewcommand\thesubsection{\arabic{section}.\arabic{subsection}}
\def\subsection@cntformat{\thesubsection.\quad}
\makeatother