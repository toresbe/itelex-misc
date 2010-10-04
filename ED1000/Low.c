/**************************************************************
WinFilter version 0.8
http://www.winfilter.20m.com
akundert@hotmail.com

Filter type: Band Pass
Filter model: Butterworth
Filter order: 1
Sampling Frequency: 14 KHz
Fc1 and Fc2 Frequencies: 2.000000 KHz and 2.550000 KHz
Coefficents Quantization: 8-bit

Z domain Zeros
z = -1.000000 + j 0.000000
z = 1.000000 + j 0.000000

Z domain Poles
z = 0.491201 + j -0.737243
z = 0.491201 + j 0.737243
***************************************************************/
#define Ntap 31

#define DCgain 256

__int8 fir(__int8 NewSample) {
    __int8 FIRCoef[Ntap] = { 
           -2,
            1,
            4,
            4,
           -1,
           -7,
           -8,
           -1,
           10,
           15,
            4,
          -19,
          -34,
          -26,
           27,
           75,
           27,
          -26,
          -34,
          -19,
            4,
           15,
           10,
           -1,
           -8,
           -7,
           -1,
            4,
            4,
            1,
           -2
    };

    static __int8 x[Ntap]; //input samples
    __int16 y=0;            //output sample
    int n;

    //shift the old samples
    for(n=Ntap-1; n>0; n--)
       x[n] = x[n-1];

    //Calculate the new output
    x[0] = NewSample;
    for(n=0; n<Ntap; n++)
        y += FIRCoef[n] * x[n];
    
    return y / DCgain;
}
