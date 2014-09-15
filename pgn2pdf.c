#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FUNCTION_STUB fprintf(stderr, "Function not implemented %s\n", __func__);
#define NOT_IMPLEMENTED { FUNCTION_STUB; }
#define NOT_IMPLEMENTED_RET(a) { FUNCTION_STUB; return a; }

#define streq(a,b) (0==strcmp(a,b))

#define PIECE_CLASS(a) (a>>6)
#define PIECE_CLASS_NOT_EQUAL(a,b) ((a>>6)^b)
#define PIECE_PLACE(a) (a&0x3f)
#define SET_PIECE_PLACE(a,b) a=(a&0x1c0)|(b&0x3f)
#define SET_PIECE_PLACE_CLASS(a,b,c) a=(c<<6)|(b&0x3f)
#define SET_PIECE_CLASS(a,c) a=(c<<6)|(a&0x3f)

#define POSITION(r,c) ((r&0x07)<<3)|(c&0x07)

#define DELIMITERS ". \r\n"

const char pre_boards_str[] = "\\documentclass[12pt,a4paper,oneside,notitlepage]{book}\n\
\\usepackage{makeidx}\n\
\\usepackage{lmodern}\n\
\\usepackage[left=1cm,right=1cm,top=1cm,bottom=1cm]{geometry}\n\
\\usepackage[utf8]{inputenc}\n\
\\usepackage[russian]{babel}\n\
\\usepackage{graphicx}\n\
\\usepackage{nopageno}\n\
\\usepackage{array}\n\
\n\
\\graphicspath{{./pics/}}\n\
\n\
\\begin{document}\n\
\n\
\\newcolumntype{D}[1]{%%\n\
 >{\\vbox to 1.335cm\\bgroup\\vfill\\centering}%%\n\
 p{#1}%%\n\
 <{\\egroup}}\n\
\n";

const char post_boards_str[] = "\
\\end{document}";

const char move_before_board_str[] = "\
\\clearpage\n";

const char board_start_str[] = "\
\\centering\n\
\\begin{tabular}{D{5mm}D{1.58cm}D{1.58cm}D{1.58cm}D{1.58cm}D{1.58cm}D{1.58cm}D{1.58cm}D{1.58cm}D{15mm}D{1mm}}\n\
&\\LARGE{a}&\\LARGE{b}&\\LARGE{c}&\\LARGE{d}&\\LARGE{e}&\\LARGE{f}&\\LARGE{g}&\\LARGE{h}&& \\\\\n";

const char board_finish_str[] = "\
&\\LARGE{a}&\\LARGE{b}&\\LARGE{c}&\\LARGE{d}&\\LARGE{e}&\\LARGE{f}&\\LARGE{g}&\\LARGE{h}&& \\\\\n\
\\end{tabular}\n";

char *white_name;
char *black_name;

/* typedefs */
// read_* functions result type
typedef enum {
    failed = 0,
    success = 1
} reader_result_t;

// move type (who wins on the move)
enum move_type_t {
    continous = 0x00,
    white_wins = 0x01,
    black_wins = 0x02,
    draw = 0x03,
    stalemate = 0x10,
    check = 0x20,
};

// pieces type
enum piece_t {
    pawn = 0x00,        // 000
    rook = 0x01,        // 001
    knight = 0x02,      // 010
    bishop = 0x03,      // 011
    queen = 0x04,       // 100
    king = 0x05,        // 101
    king_moved = 0x06,  // 110
    no_piece = 0x07     // 111
};

// piece color type (just one bit to use)
enum piece_color_t {
    white = 0x00,
    black = 0x01
};

// castling type
enum castling_type_t {
    no_castling = 0x00,
    kingside_castling = 0x01,
    queenside_castling = 0x02
};

// move structure
typedef struct {
    // what piece to move
    enum piece_t piece;
    // target piece (on promotion), no_piece otherwise
    enum piece_t target_piece;
    // check/checkmate
    enum move_type_t move_type;
    // castling type
    enum castling_type_t castling;
    // is capture?
    char capture;
} move_t;

// board type
typedef struct {
    // pieces of white and black side
    // calc as: whites[i] = (piece_type << 6) + pos
    unsigned short int whites[17];   // 16 - number of pieces, + reserved for idx=-1
    unsigned short int blacks[17];
    unsigned short int *real_whites;
    unsigned short int *real_blacks;
    // 64 bytes of the board itself
    // calc as: square[(row << 3) + col] = (color << 7) + piece_type
    unsigned char square[64];
} board_t;
/*
 * whites/blacks each byte structure:
 * bits 0..5 - piece position (values 0..63)
 * bits 6..8 - piece type, as described in enum piece_t
 * ------------
 * whites/blacks sequence:
 * indices  -   piece type
 * 0..7     -   pawn    P
 * 8..9     -   rook    R
 * 10..11   -   knight  N
 * 12..13   -   bishop  B
 * 14       -   queen   Q
 * 15       -   king    K
 *
 * the less index is the more queenee is side
 *          (left-to-right as from white-side)
 */

// indices
enum pieces_indices_t {
    pawn1_idx = 0,
    pawn2_idx,
    pawn3_idx,
    pawn4_idx,
    pawn5_idx,
    pawn6_idx,
    pawn7_idx,
    pawn8_idx,
    rook_queen_idx = 8,
    rook_king_idx,
    knight_queen_idx = 10,
    knight_king_idx,
    bishop_queen_idx = 12,
    bishop_king_idx,
    queen_idx = 14,
    king_idx = 15
};

/* functions */
/*
 * input in - string with file-data read
 *       idx - pointer to int where to store index of starting game
 *       len - size of in string
 * output idx - index to strating game in char *in
 */
reader_result_t
find_next_game(char *in, int *idx, int len)
{
    char *found;
    found = strstr(in+idx[0], "[Event");
    if (NULL == found) return failed;

    idx[0] = found - in;
    return success;
}

reader_result_t
read_white_black(char *in, int idx, int len)
{
    char *found, *found2;
    if (idx >= len) return failed;
    in += idx;
    found = strstr(in, "[White");
    if (NULL != found) {
        found = strstr(found, "\"");
        if (NULL != found) {
            ++found;
            found2 = strstr(found, "\"");
            if (NULL != found2) {
                white_name = malloc(found2 - found+1);
                memcpy(white_name, found, found2-found);
                white_name[found2-found] = '\0';
            } else {
                white_name = strdup("?");
            }
        } else {
            white_name = strdup("?");
        }
    } else {
        white_name = strdup("?");
    }

    found = strstr(in, "[Black");
    if (NULL != found) {
        found = strstr(found, "\"");
        if (NULL != found) {
            ++found;
            found2 = strstr(found, "\"");
            if (NULL != found2) {
                black_name = malloc(found2 - found+1);
                memcpy(black_name, found, found2-found);
                black_name[found2-found] = '\0';
            } else {
                black_name = strdup("?");
            }
        } else {
            black_name = strdup("?");
        }
    } else {
        black_name = strdup("?");
    }

    return success;
}

/*
 * input in - string with file-data read
 *       idx - pointer to int where to store index of movetext_section
 *       len - size of in string
 * output idx - index to movetext_section in char *in
 */
void
goto_moves(char *in, int *idx, int len)
{
    int i;
    i = idx[0];
    while (i < len) {
        // TODO
        if ('[' == in[i]) {
            while ((i < len) && (in[i] != ']')) ++i;
            continue;
        }

        if ('\n' == in[i])
            if ('\n' == in[i+1] && isdigit(in[i+2])) {
                idx[0] = i + 2;
                return;
            }

        if (isdigit(in[i])) {
            idx[0] = i;
            return;
        }

        ++i;
    }

    idx[0] = i;
}

/*
 * input: pieces - pieces list
 *        piece - piece type to find
 *        offset - from what index to start looking for piece
 * output: return index, or -1 if not found
 */
inline int
find_piece(register unsigned short int *pieces,
           register enum piece_t piece,
           register int offset)
{
    register int i;

    if (piece == king || piece == king_moved)
        return king_idx;

    for (i = offset; i < 16; ++i)
        if (!PIECE_CLASS_NOT_EQUAL(pieces[i], piece)) {
            return i;
        }

    return -1;
}

/*
 * input: pieces - pieces list
 *        pos - position to find
 *        offset - from what index to start looking for piece
 * output: return index, or -1 if not found
 */
inline int
find_piece_by_pos(register unsigned short int *pieces,
                  register unsigned char pos,
                  register int offset)
{
    register int i;

    for (i = offset; i < 16; ++i)
        if (!(PIECE_PLACE(pieces[i]) ^ pos)) {
            if (PIECE_CLASS_NOT_EQUAL(pieces[i],no_piece)) return i;
        }

    return -1;
}

/*
 * input: pieces - pieces list
 *        col - column to find
 *        piece - piece to find
 *        offset - from what index to start looking for piece
 * output: return index, or -1 if not found
 */
inline int
find_piece_by_col(register unsigned short int *pieces,
                  register unsigned char col,
                  register unsigned char piece,
                  register int offset)
{
    register int i;

    for (i = offset; i < 16; ++i)
        if (!((PIECE_PLACE(pieces[i]) & 0x07) ^ col) ) {
            if (!PIECE_CLASS_NOT_EQUAL(pieces[i],piece)) return i;
        }

    return -1;
}

/*
 * input: pieces - pieces list
 *        row - row to find
 *        piece - piece to find
 *        offset - from what index to start looking for piece
 * output: return index, or -1 if not found
 */
inline int
find_piece_by_row(register unsigned short int *pieces,
                  register unsigned char row,
                  register unsigned char piece,
                  register int offset)
{
    register int i;

    for (i = offset; i < 16; ++i)
        if (!(((PIECE_PLACE(pieces[i]) >> 3) & 0x07 ) ^ row) )
            if (!PIECE_CLASS_NOT_EQUAL(pieces[i],piece)) return i;

    return -1;
}

/*
 * input: pieces - pieces list
 *        idx - what piece
 *        dest_pos - to what square
 *        capture_black - is capturing move (bit 1)? (for pawn)
 *                        is moving as blacks (bit 0)? (for pawn)
 * output: return 0 - cannot move, 1 - can move
 */
inline int
can_move(unsigned short int *pieces,
         unsigned short int *other_pieces,
         unsigned char idx,
         unsigned char dest_pos,
         unsigned char capture_black)
{
    register unsigned char from_col = PIECE_PLACE(pieces[idx]) & 0x07;
    register unsigned char from_row = (PIECE_PLACE(pieces[idx]) >> 3) & 0x07;
    register unsigned char dest_col = dest_pos & 0x07;
    register unsigned char dest_row = (dest_pos >> 3) & 0x07;
    register unsigned char max_row, min_row, max_col, min_col, t;
    unsigned char temp_pos;
    char dr, dc, d, tc, tr;
    // TODO
    switch (PIECE_CLASS(pieces[idx])) {
        case pawn:
            // white pawn move by one row
            if ((dest_row - from_row) == 1 &&
                (capture_black & 0x01) == 0) {
                if (((dest_col - from_col) == 1 ||
                     (from_col - dest_col) == 1) &&
                    (capture_black & 0x02) != 0) {
                    return 1;
                }

                if ((dest_col == from_col) &&
                    (capture_black & 0x02) == 0) {
                    return 1;
                }
            }

            // black pawn move by one row
            if ((from_row - dest_row) == 1 &&
                (capture_black & 0x01) != 0) {
                if (((dest_col - from_col) == 1 ||
                     (from_col - dest_col) == 1) &&
                    (capture_black & 0x02) != 0) {
                    return 1;
                }

                if ((dest_col == from_col) &&
                    (capture_black & 0x02) == 0) {
                    return 1;
                }
            }

            // white pawn move by two rows
            if ((dest_row - from_row) == 2 && (dest_col == from_col) &&
                (capture_black & 0x01) == 0 && (capture_black & 0x02) == 0) {
                if (from_row == 1) {
                    temp_pos = POSITION(2,dest_col);
                    if (find_piece_by_pos(pieces,temp_pos,0) != -1)
                        return 0;
                    return 1;
                }
            }

            // black pawn move by two rows
            if ((from_row - dest_row) == 2 && (dest_col == from_col) &&
                (capture_black & 0x01) != 0 && (capture_black & 0x02) == 0) {
                if (from_row == 6) {
                    temp_pos = POSITION(5,dest_col);
                    if (find_piece_by_pos(pieces,temp_pos,0) != -1)
                        return 0;
                    return 1;
                }
            }

            return 0;
            break;

        case rook:
            if ((dest_col != from_col) &&
                (dest_row == from_row)) {
                if (dest_col > from_col) {
                    max_col = dest_col;
                    min_col = from_col;
                } else {
                    max_col = from_col;
                    min_col = dest_col;
                }

                for (t = min_col+1; t < max_col; ++t ) {
                    temp_pos = POSITION(dest_row,t);
                    if (find_piece_by_pos(pieces,temp_pos,0) != -1) return 0;
                    if (find_piece_by_pos(other_pieces,temp_pos,0) != -1) return 0;
                }

                return 1;
            }

            if ((dest_col == from_col) &&
                (dest_row != from_row)) {
                if (dest_row > from_row) {
                    max_row = dest_row;
                    min_row = from_row;
                } else {
                    max_row = from_row;
                    min_row = dest_row;
                }

                for (t = min_row+1; t < max_row; ++t ) {
                    temp_pos = POSITION(t,dest_col);
                    if (find_piece_by_pos(pieces,temp_pos,0) != -1) return 0;
                    if (find_piece_by_pos(other_pieces,temp_pos,0) != -1) return 0;
                }

                return 1;
            }

            return 0;
            break;

        case knight:
            if (dest_row > from_row) {
                max_row = dest_row;
                min_row = from_row;
            } else {
                max_row = from_row;
                min_row = dest_row;
            }

            if (dest_col > from_col) {
                max_col = dest_col;
                min_col = from_col;
            } else {
                max_col = from_col;
                min_col = dest_col;
            }

            if ((max_row - min_row) == 2 &&
                (max_col - min_col) == 1) return 1;

            if ((max_row - min_row) == 1 &&
                (max_col - min_col) == 2) return 1;

            return 0;
            break;

        case bishop:
            // TODO: check if there is no piece standing on the way
            if (dest_row > from_row) {
                max_row = dest_row;
                min_row = from_row;
                dr = +1;
            } else {
                max_row = from_row;
                min_row = dest_row;
                dr = -1;
            }

            if (dest_col > from_col) {
                max_col = dest_col;
                min_col = from_col;
                dc = +1;
            } else {
                max_col = from_col;
                min_col = dest_col;
                dc = -1;
            }

            if ((max_col - min_col) == (max_row - min_row)) {
                d = max_col - min_col;
                tr = from_row + dr;
                tc = from_col + dc;
                for (t = 1; t < d; ++t) {
                    temp_pos = POSITION(tr, tc);
                    if (find_piece_by_pos(pieces, temp_pos, 0) != -1) return 0;
                    if (find_piece_by_pos(other_pieces,temp_pos,0) != -1) return 0;

                    tr += dr;
                    tc += dc;
                }
                return 1;
            }

            return 0;
            break;

        case queen:
            // if moving as rook
            if ((dest_col != from_col) &&
                (dest_row == from_row)) {
                if (dest_col > from_col) {
                    max_col = dest_col;
                    min_col = from_col;
                } else {
                    max_col = from_col;
                    min_col = dest_col;
                }

                for (t = min_col+1; t < max_col; ++t ) {
                    temp_pos = POSITION(dest_row,t);
                    if (find_piece_by_pos(pieces,temp_pos,0) != -1) return 0;
                    if (find_piece_by_pos(other_pieces,temp_pos,0) != -1) return 0;
                }

                return 1;
            }

            if ((dest_col == from_col) &&
                (dest_row != from_row)) {
                if (dest_row > from_row) {
                    max_row = dest_row;
                    min_row = from_row;
                } else {
                    max_row = from_row;
                    min_row = dest_row;
                }

                for (t = min_row+1; t < max_row; ++t ) {
                    temp_pos = POSITION(t,dest_col);
                    if (find_piece_by_pos(pieces,temp_pos,0) != -1) return 0;
                    if (find_piece_by_pos(other_pieces,temp_pos,0) != -1) return 0;
                }

                return 1;
            }

            // or moving as a bishop
            if (dest_row > from_row) {
                max_row = dest_row;
                min_row = from_row;
                dr = +1;
            } else {
                max_row = from_row;
                min_row = dest_row;
                dr = -1;
            }

            if (dest_col > from_col) {
                max_col = dest_col;
                min_col = from_col;
                dc = +1;
            } else {
                max_col = from_col;
                min_col = dest_col;
                dc = -1;
            }

            if ((max_col - min_col) == (max_row - min_row)) {
                d = max_col - min_col;
                tr = from_row + dr;
                tc = from_col + dc;
                for (t = 1; t < d; ++t) {
                    temp_pos = POSITION(tr, tc);
                    if (find_piece_by_pos(pieces, temp_pos, 0) != -1) return 0;
                    if (find_piece_by_pos(other_pieces,temp_pos,0) != -1) return 0;

                    tr += dr;
                    tc += dc;
                }
                return 1;
            }

            return 0;
            break;

        case king_moved:
        case king:
            if (dest_row > from_row) {
                max_row = dest_row;
                min_row = from_row;
            } else {
                max_row = from_row;
                min_row = dest_row;
            }

            if (dest_col > from_col) {
                max_col = dest_col;
                min_col = from_col;
            } else {
                max_col = from_col;
                min_col = dest_col;
            }

            if ((max_col - min_col) == 0 &&
                (max_row - min_row) == 0) return 0;

            if ((max_col - min_col) <= 1 &&
                (max_row - min_row) <= 1) return 1;

            return 0;
            break;
    }

    return 0;
}

inline int
castling_move(char *move_str,
              unsigned short int *pieces,
              move_t *move,
              char black)
{
    register unsigned char temp_pos;
     //  if king ever moved
    if (PIECE_CLASS_NOT_EQUAL(pieces[king_idx], king))
        return -1;

    // perform castling
    if (move->castling & 0x01) {
        // kingside castling
        // choose kingside rook
        if (PIECE_CLASS_NOT_EQUAL(pieces[rook_king_idx], rook))
            return -1;

        // do the castling
        temp_pos = PIECE_PLACE(pieces[king_idx]) + 2;
        SET_PIECE_PLACE(pieces[king_idx], temp_pos);

        temp_pos = PIECE_PLACE(pieces[rook_king_idx]) - 2;
        SET_PIECE_PLACE(pieces[rook_king_idx], temp_pos);
    } else {
        // queenside castling
        // choose kingside rook
        if (PIECE_CLASS_NOT_EQUAL(pieces[rook_queen_idx], rook))
            return -1;

        // do the castling
        temp_pos = PIECE_PLACE(pieces[king_idx]) - 2;
        SET_PIECE_PLACE(pieces[king_idx], temp_pos);

        temp_pos = PIECE_PLACE(pieces[rook_queen_idx]) + 3;
        SET_PIECE_PLACE(pieces[rook_queen_idx], temp_pos);
    }

    SET_PIECE_CLASS(pieces[king_idx], king_moved);
    return 0;
}

inline int
capturing_move(char *move_str,
               unsigned short int *pieces,
               unsigned short int *other_pieces,
               move_t *move,
               char black,
               char *temp_found)
{
    char piece_char;
    register int temp_idx, temp_idx2;
    register unsigned char temp_pos;
    register unsigned char dest_pos;
    register int move_str_len;
    move->capture = 1;
    dest_pos = POSITION(temp_found[2] - 0x31, temp_found[1] - 'a');

    // check for hints before 'x'
    if (temp_found - move_str == 3) {
        // like 'Ng5xf7'
        temp_pos = POSITION(move_str[2] - 0x31, move_str[1] - 'a');
        // find piece with position dest_pos
        temp_idx = find_piece_by_pos(other_pieces, dest_pos, 0);
        SET_PIECE_CLASS(other_pieces[temp_idx], no_piece);

        temp_idx = find_piece_by_pos(pieces, temp_pos, 0);
        SET_PIECE_PLACE(pieces[temp_idx], dest_pos);
        return 0;
    }

    if (temp_found - move_str == 2) {
        // like Rgxa8
        if (isalpha(move_str[1])) {
            temp_idx = -1;

            do {
                temp_idx = find_piece_by_col(pieces,
                                             move_str[1] - 'a',
                                             move->piece,
                                             temp_idx + 1);

                if (temp_idx < 0)
                    return -1;
            } while (!can_move(pieces, other_pieces, temp_idx, dest_pos, black | 0x02));

            // temp_idx is index to piece that can move to dest_pos
            temp_idx2 = find_piece_by_pos(other_pieces, dest_pos, 0);
            SET_PIECE_CLASS(other_pieces[temp_idx2], no_piece);
            SET_PIECE_PLACE(pieces[temp_idx], dest_pos);
            return 0;
        }

        // like 'R2xa8
        if (isdigit(move_str[1])) {
            temp_idx = -1;

            do {
                temp_idx = find_piece_by_row(pieces,
                                             move_str[1] - 0x31,
                                             move->piece,
                                             temp_idx + 1);

                if (temp_idx < 0)
                    return -1;
            } while (!can_move(pieces, other_pieces, temp_idx, dest_pos, black | 0x02));

            // temp_idx is index to piece that can move to dest_pos
            temp_idx2 = find_piece_by_pos(other_pieces, dest_pos, 0);
            SET_PIECE_CLASS(other_pieces[temp_idx2], no_piece);
            SET_PIECE_PLACE(pieces[temp_idx], dest_pos);
            return 0;
        }

        return -1;
    }

    if (temp_found - move_str == 1) {
        // check for pawn capture and en passant capture
        if (move->piece == pawn) {
            // like 'exf5'
            // find pawn that can move to dest_pos
            temp_idx = -1;
            do {
                temp_idx = find_piece_by_col(pieces,
                                             move_str[0] - 'a',
                                             move->piece,
                                             temp_idx + 1);

                if (temp_idx < 0) return -1;
            } while (!can_move(pieces, other_pieces, temp_idx, dest_pos, black | 0x02));

            // catch pawn captures and en passant captures
            temp_idx2 = find_piece_by_pos(other_pieces, dest_pos, 0);
            if (temp_idx2 == -1) {
                // en passant capture
                // check for pawn on delta_row from dest_pos
                if (black) temp_pos = dest_pos + 1 * 8;
                else temp_pos = dest_pos - 1 * 8;

                temp_idx2 = find_piece_by_pos(other_pieces, temp_pos, 0);
                if (temp_idx2 == -1) return -1;
                if (PIECE_CLASS_NOT_EQUAL(pieces[temp_idx2], pawn)) return -1;
            }
            // pawn capture
            SET_PIECE_CLASS(other_pieces[temp_idx2], no_piece);
            SET_PIECE_PLACE(pieces[temp_idx], dest_pos);

            if (move->target_piece != no_piece)
                SET_PIECE_CLASS(pieces[temp_idx], move->target_piece);

            return 0;
        }

        // like Nxg5
        temp_idx = -1;
        do {
            temp_idx = find_piece(pieces,
                                  move->piece,
                                  temp_idx + 1);

            if (temp_idx < 0)
                return -1;
        } while (!can_move(pieces, other_pieces, temp_idx, dest_pos, black | 0x02));

        // temp_idx is index to piece that can move to dest_pos
        temp_idx2 = find_piece_by_pos(other_pieces, dest_pos, 0);
        // temp_idx2 is index to piece that is at the dest_pos
        SET_PIECE_CLASS(other_pieces[temp_idx2], no_piece);
        SET_PIECE_PLACE(pieces[temp_idx], dest_pos);

        if (PIECE_CLASS(pieces[temp_idx]) == king)
            SET_PIECE_CLASS(pieces[temp_idx], king_moved);

        return 0;
    }

    return -1;
}

/*
 * input: move_str - move representation (white/black)
 *        pieces - pieces list (white/black)
 *        other_pieces - pieces of other player (black/white)
 *        move - move structure
 *        black - moving as blacks? (bit 0)
 * output: return result - 0 - ok, != 0 - fail
 *                move - move structure
 */
static int
parse_move(char *move_str,
           unsigned short int *pieces,
           unsigned short int* other_pieces,
           move_t *move,
           char black)
{
    char piece_char;
    char *temp_found;
    register int temp_idx, temp_idx2;
    register unsigned char temp_pos;
    register unsigned char dest_pos;
    register int move_str_len;
    int result;

    if (islower(move_str[0])) piece_char = 'P';
    else piece_char = move_str[0];

    move->castling = no_castling;

    switch (piece_char) {
        case 'P' :
            move->piece = pawn;
            break;

        case 'R' :
            move->piece = rook;
            break;

        case 'N' :
            move->piece = knight;
            break;

        case 'B' :
            move->piece = bishop;
            break;

        case 'Q' :
            move->piece = queen;
            break;

        case 'K' :
            if (PIECE_CLASS_NOT_EQUAL(pieces[king_idx], king))
                move->piece = king_moved;
            else
                move->piece = king;
            break;

        case 'O' :
            move->piece = no_piece;
            move->castling = streq(move_str, "O-O-O") ?
                             queenside_castling :
                             kingside_castling;
            break;

        case '0' :
            move->piece = no_piece;
            move->castling = streq(move_str, "0-0-0") ?
                             queenside_castling :
                             kingside_castling;
            break;

        default :
            fprintf(stderr, "Unknown piece in move %s\n", move_str);
            return -1;
            break;
    }

    if (move->castling) {
        result = castling_move(move_str, pieces, move, black);
        return result;
    }

    // check for check '+'
    temp_found = strstr(move_str, "+");
    move->move_type = continous;
    if (NULL != temp_found) {
        move->move_type = check;
        temp_found[0] = '\0';
    }

    // check for checkmate '#'
    temp_found = strstr(move_str, "#");
    if (NULL != temp_found) {
        move->move_type = black ? black_wins : white_wins;
        temp_found[0] = '\0';
    }

    // check for promotion '='
    move->target_piece = no_piece;
    temp_found = strstr(move_str, "=");
    if (NULL != temp_found) {
        temp_found[0] = '\0';
        ++temp_found;
        switch (temp_found[1]) {
            case 'Q' :
                move->target_piece = queen;
                break;
            case 'B':
                move->target_piece = bishop;
                break;
            case 'N':
                move->target_piece = knight;
                break;
            case 'R':
                move->target_piece = rook;
                break;
            default:
                return -1;
        }
    }

    // TODO: check and skip glyphs (like !, ?, $, (, etc.)
    //       annotations: !, ?, !!, ??, !?, ?!
    temp_found = strstr(move_str, "!");
    if (NULL != temp_found)
        temp_found[0] = '\0';

    temp_found = strstr(move_str, "?");
    if (NULL != temp_found)
        temp_found[0] = '\0';

    //       NAG (numeric annotation glyph): $<integer 0..255>
    temp_found = strstr(move_str, "$");
    if (NULL != temp_found)
        temp_found[0] = '\0';

    //       RAV (recursive annotation variation): (...)
    temp_found = strstr(move_str, "(");
    if (NULL != temp_found)
        temp_found[0] = '\0';

    //      comments: {...}
    temp_found = strstr(move_str, "{");
    if (NULL != temp_found)
        temp_found[0] = '\0';
    // check move types:
    //      - capture move
    temp_found = strstr(move_str, "x");

    if (NULL != temp_found) {
        // it is capturing move
        result = capturing_move(move_str, pieces, other_pieces, move, black, temp_found);
        return result;
    }
    move->capture = 0;

    // TODO: general move
    // look for hints for from-square
    if (move->piece == pawn) {
        // if moving pawn there should be no hints, then
        dest_pos = POSITION(move_str[1] - 0x31, move_str[0] - 'a');
        // find the pawn that can move to dest_pos
        temp_idx = -1;
        do {
            temp_idx = find_piece(pieces,
                                  move->piece,
                                  temp_idx + 1);

            if (temp_idx < 0)
                return -1;
        } while (!can_move(pieces, other_pieces, temp_idx, dest_pos, black));

        // move the pawn
        SET_PIECE_PLACE(pieces[temp_idx], dest_pos);
        if (move->target_piece != no_piece)
            SET_PIECE_CLASS(pieces[temp_idx], move->target_piece);

        return 0;
    }

    move_str_len = strlen(move_str);
    dest_pos = POSITION(move_str[move_str_len - 1] - 0x31, move_str[move_str_len - 2] - 'a');

    if (move_str_len == 4) {
        // like Ngf7
        if (isalpha(move_str[1])) {
            temp_idx = -1;
            do {
                temp_idx = find_piece_by_col(pieces,
                                             move_str[1] - 'a',
                                             move->piece,
                                             temp_idx + 1);

                if (temp_idx < 0)
                    return -1;
            } while (!can_move(pieces, other_pieces, temp_idx, dest_pos, black));

            // move the piece
            SET_PIECE_PLACE(pieces[temp_idx], dest_pos);
            return 0;
        }

        // like N5f7
        if (isdigit(move_str[1])) {
            temp_idx = -1;
            do {
                temp_idx = find_piece_by_row(pieces,
                                             move_str[1] - 0x31,
                                             move->piece,
                                             temp_idx + 1);

                if (temp_idx < 0)
                    return -1;
            } while (!can_move(pieces, other_pieces, temp_idx, dest_pos, black));

            // move the piece
            SET_PIECE_PLACE(pieces[temp_idx], dest_pos);
            return 0;
        }

        return -1;
    }

    if (move_str_len == 3) {
        // like Nf7
        temp_idx = -1;
        do {
            temp_idx = find_piece(pieces,
                                  move->piece,
                                  temp_idx + 1);

            if (temp_idx < 0)
                return -1;
        } while (!can_move(pieces, other_pieces, temp_idx, dest_pos, black));

        // move the piece
        SET_PIECE_PLACE(pieces[temp_idx], dest_pos);

        return 0;
    }

    return -1;
}

/*
 * input: board - board to convert to 64 byte array
 * output: board - board with refreshed 64 byte array
 */
void
convert_board(board_t *board)
{
    unsigned char place;
    unsigned char class;
    int i;
    enum piece_color_t color;
    // write zeros first
    bzero(board->square, sizeof(board->square));
    // set all squares of board to no_piece
    memset(board->square, no_piece, sizeof(board->square));

    color = white;
    for (i = 0; i < 16; ++i) {
        place = PIECE_PLACE(board->real_whites[i]);
        class = PIECE_CLASS(board->real_whites[i]);
        if (class == no_piece) continue;
        board->square[place] = (color<<7) | class;
    }

    color = black;
    for (i = 0; i < 16; ++i) {
        place = PIECE_PLACE(board->real_blacks[i]);
        class = PIECE_CLASS(board->real_blacks[i]);
        if (class == no_piece) continue;
        board->square[place] = (color<<7) | class;
    }
}

void
print_board(FILE* out,
            board_t *board,
            const char *move_str,
            int black,
            int move_nr)
{
    int row, col;
    char fig_name[4];
    unsigned char place;
    unsigned char class;
    enum piece_color_t color;
    char square_color[2] = {
        [0] = 'b',
        [1] = 'w'
    };

    char piece_color[2] = {
        [1] = 'b',
        [0] = 'w'
    };

    fprintf(out, move_before_board_str);

    fprintf(out,
            "\\begin{Large} %s~---~%s \\end{Large}\n\\linebreak~\\linebreak\
\\begin{Large} %d. \\verb|%s| \\end{Large}\n",
            white_name,
            black_name,
            move_nr,
            move_str);

    fprintf(out, board_start_str);

    for (row = 7; row >= 0; --row) {
        fprintf(out, "\t \\LARGE{%d} ", row+1);
        for (col = 0; col < 8; ++col) {
            place = POSITION(row,col);
            class = board->square[place] & 0x07;
            color = (board->square[place]>>7) & 0x01;
            fig_name[0] = piece_color[color];
            switch (class) {
                case pawn: fig_name[1]='p';
                           break;
                case rook: fig_name[1]='r';
                           break;
                case knight: fig_name[1] = 'n';
                             break;
                case bishop: fig_name[1] = 'b';
                             break;
                case queen: fig_name[1] = 'q';
                            break;
                case king_moved:
                case king: fig_name[1] = 'k';
                           break;
                case no_piece: fig_name[0] = fig_name[1] = 'x';
                               break;
            }

            fig_name[2] = square_color[(row & 0x01) ^ (col & 0x01)];
            fig_name[3] = '\0';

            fprintf(out, "& \\includegraphics[width=2cm,height=2cm]{%s} ", fig_name);
        }
        fprintf(out, " & \\LARGE{%d} & \\\\\n", row+1);
    }

    fprintf(out, board_finish_str);
}

reader_result_t
check_finish (const char* token, FILE* out)
{
           if (streq(token, "1-0")) {
                return success;
            }

            if (streq(token, "0-1")) {
                return success;
            }

            if (streq(token, "1/2-1/2")) {
                return success;
            }

            if (streq(token, "*")) {
                return success;
            }

            return failed;
}

reader_result_t
read_moves(char *movetext_section,
           FILE *out,
           board_t *board)
{
    int move_nr;
    move_t move;
    char *move_white;
    char *move_black;
    char *token;
    int result;
    int is_black = 0;
    char *move_to_print;
    char *movetext_section2;

    movetext_section2 = strdup(movetext_section);
    // tokenize movetext_section
    move_to_print = malloc(255+1);
    token = strtok(movetext_section2, DELIMITERS);
    while (token) {
        move_nr = atoi(token);

        if (0 == move_nr) break;

        token = strtok(NULL, DELIMITERS);
        if (NULL == token) break;
        move_white = strdup(token);

        token = strtok(NULL, DELIMITERS);
        if (NULL == token) move_black = strdup("");
        move_black = strdup(token);

        sprintf(move_to_print, "%s %s", move_white, move_black);
        /*printf("Parsing %d. %s\n", move_nr, move_to_print);*/

        if (check_finish(move_white, out) == success) break;
        is_black = 0;
        result = parse_move(move_white,
                            board->real_whites,
                            board->real_blacks,
                            &move,is_black);

        convert_board(board);
        print_board(out, board, move_to_print, is_black, move_nr);

        if (check_finish(move_black, out) == success) break;
        is_black = 1;
        result = parse_move(move_black,
                            board->real_blacks,
                            board->real_whites,
                            &move,is_black);

        convert_board(board);
        print_board(out, board, move_to_print, black, move_nr);

        free(move_white);
        free(move_black);

        token = strtok(NULL,DELIMITERS);
    }

    free(move_to_print);
    free(movetext_section2);
    return success;
}

board_t *
make_new_board(FILE *out,
               board_t *board)
{
    /* place black pieces */
    SET_PIECE_PLACE_CLASS(board->real_blacks[pawn1_idx],POSITION(6,0),pawn);
    SET_PIECE_PLACE_CLASS(board->real_blacks[pawn2_idx],POSITION(6,1),pawn);
    SET_PIECE_PLACE_CLASS(board->real_blacks[pawn3_idx],POSITION(6,2),pawn);
    SET_PIECE_PLACE_CLASS(board->real_blacks[pawn4_idx],POSITION(6,3),pawn);
    SET_PIECE_PLACE_CLASS(board->real_blacks[pawn5_idx],POSITION(6,4),pawn);
    SET_PIECE_PLACE_CLASS(board->real_blacks[pawn6_idx],POSITION(6,5),pawn);
    SET_PIECE_PLACE_CLASS(board->real_blacks[pawn7_idx],POSITION(6,6),pawn);
    SET_PIECE_PLACE_CLASS(board->real_blacks[pawn8_idx],POSITION(6,7),pawn);

    SET_PIECE_PLACE_CLASS(board->real_blacks[rook_queen_idx],POSITION(7,0),rook);
    SET_PIECE_PLACE_CLASS(board->real_blacks[rook_king_idx],POSITION(7,7),rook);

    SET_PIECE_PLACE_CLASS(board->real_blacks[knight_queen_idx],POSITION(7,1),knight);
    SET_PIECE_PLACE_CLASS(board->real_blacks[knight_king_idx],POSITION(7,6),knight);

    SET_PIECE_PLACE_CLASS(board->real_blacks[bishop_queen_idx],POSITION(7,2),bishop);
    SET_PIECE_PLACE_CLASS(board->real_blacks[bishop_king_idx],POSITION(7,5),bishop);

    SET_PIECE_PLACE_CLASS(board->real_blacks[queen_idx],POSITION(7,3),queen);
    SET_PIECE_PLACE_CLASS(board->real_blacks[king_idx],POSITION(7,4),king);

    /* place white pieces */
    SET_PIECE_PLACE_CLASS(board->real_whites[pawn1_idx],POSITION(1,0),pawn);
    SET_PIECE_PLACE_CLASS(board->real_whites[pawn2_idx],POSITION(1,1),pawn);
    SET_PIECE_PLACE_CLASS(board->real_whites[pawn3_idx],POSITION(1,2),pawn);
    SET_PIECE_PLACE_CLASS(board->real_whites[pawn4_idx],POSITION(1,3),pawn);
    SET_PIECE_PLACE_CLASS(board->real_whites[pawn5_idx],POSITION(1,4),pawn);
    SET_PIECE_PLACE_CLASS(board->real_whites[pawn6_idx],POSITION(1,5),pawn);
    SET_PIECE_PLACE_CLASS(board->real_whites[pawn7_idx],POSITION(1,6),pawn);
    SET_PIECE_PLACE_CLASS(board->real_whites[pawn8_idx],POSITION(1,7),pawn);

    SET_PIECE_PLACE_CLASS(board->real_whites[rook_queen_idx],POSITION(0,0),rook);
    SET_PIECE_PLACE_CLASS(board->real_whites[rook_king_idx],POSITION(0,7),rook);

    SET_PIECE_PLACE_CLASS(board->real_whites[knight_queen_idx],POSITION(0,1),knight);
    SET_PIECE_PLACE_CLASS(board->real_whites[knight_king_idx],POSITION(0,6),knight);

    SET_PIECE_PLACE_CLASS(board->real_whites[bishop_queen_idx],POSITION(0,2),bishop);
    SET_PIECE_PLACE_CLASS(board->real_whites[bishop_king_idx],POSITION(0,5),bishop);

    SET_PIECE_PLACE_CLASS(board->real_whites[queen_idx],POSITION(0,3),queen);
    SET_PIECE_PLACE_CLASS(board->real_whites[king_idx],POSITION(0,4),king);

    fprintf(out,"\\clearpage\n");

    return board;
}

void
start_boards(FILE *out)
{
    fprintf(out, pre_boards_str);
}

void
finish_boards(FILE *out)
{
    fprintf(out, post_boards_str);
}

/* main function */
int
main (int argc, char **argv)
{
    FILE *in;
    FILE *out;
    int game_start;
    move_t move_white, move_black;
    board_t *board;
    struct stat st;
    char *file_data;

    if (argc != 3) {
        printf("usage: %s <input.pgn> <output.tex>\n", argv[0]);
        return 0;
    }

    if (stat(argv[1], &st) < 0) {
        perror("cant stat input file");
        return 1;
    }

    in = fopen(argv[1], "r");
    out = fopen(argv[2], "w");

    if (NULL == in || NULL == out) {
        perror("fopen");
        return 1;
    }

    rewind(in);
    file_data = malloc(st.st_size+1);
    fread(file_data, 1, st.st_size, in);
    file_data[st.st_size] = '\0';
    fclose(in);

    board = malloc(sizeof(board_t));
    board->real_whites = &board->whites[1];
    board->real_blacks = &board->blacks[1];

    start_boards(out);

    game_start = 0;
    /*while (find_next_game(file_data, &game_start, st.st_size) == success) {*/
    find_next_game(file_data, &game_start, st.st_size);
    read_white_black(file_data, game_start, st.st_size);

    make_new_board(out, board);

    goto_moves(file_data, &game_start, st.st_size);

    read_moves(file_data + game_start, out, board);
    /*}*/

    finish_boards(out);
    fclose(out);

    free(board);

    return 0;
}
