/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TB371FC_SPEAKER_MATRIX_H
#define TB371FC_SPEAKER_MATRIX_H
#define TB371FC_MATRIX_GAIN 16384
/* Rows: fixed physical TDM slots. Columns: logical LU, RU, LL, RL.
 * The DSP sees the original input channel count, never a fake 4ch PCM. */
static inline int tb371fc_matrix_valid(const unsigned short *matrix)
{
 int row, col, sum;
 for (row = 0; row < 4; row++) {
  sum = 0;
  for (col = 0; col < 4; col++) {
   if (matrix[row * 4 + col] > TB371FC_MATRIX_GAIN)
    return 0;
   sum += matrix[row * 4 + col];
  }
  if (sum != TB371FC_MATRIX_GAIN)
   return 0;
 }
 return 1;
}
static inline unsigned short tb371fc_matrix_weight(
 const unsigned short *matrix, int channels, int row, int input)
{
 int col, weight = 0;
 for (col = 0; col < 4; col++) {
  int source = channels == 1 ? 0 : channels == 2 ? col % 2 : col;
  if (source == input)
   weight += matrix[row * 4 + col];
 }
 return weight;
}
#endif
