/**************************************************************
WinFilter version 0.8
http://www.winfilter.20m.com
akundert@hotmail.com

Filter type: Band Pass
Filter model: Chebyshev
Filter order: 2
Sampling Frequency: 12 KHz
Fc1 and Fc2 Frequencies: 1.500000 KHz and 2.000000 KHz
Pass band Ripple: 1.600000 dB
Coefficents Quantization: 8-bit

Z domain Zeros
z = -1.000000 + j 0.000000
z = -1.000000 + j 0.000000
z = 1.000000 + j 0.000000
z = 1.000000 + j 0.000000

Z domain Poles
z = 0.644042 + j -0.614071
z = 0.644042 + j 0.614071
z = 0.559083 + j -0.820982
z = 0.559083 + j 0.820982
***************************************************************/
#define NCoef 4
#define DCgain 128

__int8 iir(__int8 NewSample) {
    __int8 ACoef[NCoef+1] = {
           51,
            0,
         -103,
            0,
           51
    };

    __int8 BCoef[NCoef+1] = {
           32,
          -77,
          103,
          -69,
           25
    };

    static __int16 y[NCoef+1]; //output samples
    //Warning!!!!!! This variable should be signed (input sample width + Coefs width + 4 )-bit width to avoid saturation.

    static __int8 x[NCoef+1]; //input samples
    int n;

    //shift the old samples
    for(n=NCoef; n>0; n--) {
       x[n] = x[n-1];
       y[n] = y[n-1];
    }

    //Calculate the new output
    x[0] = NewSample;
    y[0] = ACoef[0] * x[0];
    for(n=1; n<=NCoef; n++)
        y[0] += ACoef[n] * x[n] - BCoef[n] * y[n];

    y[0] /= BCoef[0];
    
    return y[0] / DCgain;
}
