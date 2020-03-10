#include <ncurses.h>
#include <locale.h>
#include <signal.h>
#include <hidapi/hidapi.h>
#include <unistd.h> //usleep()

#define MAX_STR 20

unsigned int Device_ID;        //  уникальный номер устройства
unsigned char   USB_BUFI [9];  //  буфер приёма
unsigned char   USB_BUFO [9];  //  буфер передачи
hid_device *handle;

int                 ONEWIRE_COUNT;                      //  количество ROM
unsigned long long  ONEWIRE_ROM[128];                   //  номера ROM
float               ONEWIRE_TEMP[128];                  //  температура


void sig(int n)
{
}

void USB_BUF_CLEAR()
{   //  очистка буферов приёма и передачи
    for (int i=0; i<9; i++) { USB_BUFI[i]=0; USB_BUFO[i]=0; }
}

bool USB_GET_FEATURE()
{   //  чтение в буфер из устройства
    bool RESULT=false;
    int i=3;   //  число попыток
    while (!RESULT & ((i--)>0))
    {
        if (hid_get_feature_report(handle, USB_BUFI, 9) == -1) RESULT = false;
        else RESULT = true;
    }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка чтения из USB-устройства" << std::endl;
#endif
     return RESULT;
}

bool USB_SET_FEATURE()
{   //  запись из буфера в устройство
    bool RESULT=true;
    if (hid_send_feature_report(handle, USB_BUFO, 9) == -1) RESULT = false;
    else RESULT = true;
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка записи в USB-устройство" << std::endl;
#endif
    return RESULT;
}

bool USB_GET_ID(unsigned int * ID)
{   //  чтение ID устройства, 2ms
    USB_BUF_CLEAR();
    bool RESULT=false;
    USB_BUFO[1]=0x1D;
    int i=3;   //  число попыток
    while (!RESULT & ((i--)>0))
        if (USB_SET_FEATURE())
            if (USB_GET_FEATURE())
                if (USB_BUFI[1]==0x1D) { RESULT=true; *ID=(USB_BUFI[5]<<24)+(USB_BUFI[6]<<16)+(USB_BUFI[7]<<8)+USB_BUFI[8]; }
                    else RESULT=false;
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка чтения ID устройства" << std::endl;
#endif
    return RESULT;
}

unsigned char CRC8(unsigned char CRC, unsigned char D)
{   //  подчсёт CRC для DALLAS
    unsigned char R=CRC;
    for (int i=0; i<8; i++)
        if (((R^(D>>i))&0x01)==0x01) R=((R^0x18)>>1)|0x80;
            else R=(R>>1)&0x7F;
    return R;
}

bool OW_RESET()
{   //  RESET, ~3ms
    bool RESULT=false;
    USB_BUF_CLEAR();
    USB_BUFO[1]=0x18;    USB_BUFO[2]=0x48;
    unsigned char N=3;
    while (!RESULT &((N--)>0))
        if (USB_SET_FEATURE())
            {
            usleep(5000);
            if (USB_GET_FEATURE()) RESULT=(USB_BUFI[1]==0x18)&(USB_BUFI[2]==0x48)&(USB_BUFI[3]==0x00);
                else RESULT=false;
            }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка OW_RESET" << std::endl; ;
#endif
    return RESULT;
}

bool OW_WRITE_BYTE(unsigned char B)
{   //  запись байта, 3ms
    bool RESULT=false;
    USB_BUF_CLEAR();
    USB_BUFO[1]=0x18;    USB_BUFO[2]=0x88;    USB_BUFO[3]=B;
    if (USB_SET_FEATURE())
        {
        usleep(5000);
        if (USB_GET_FEATURE())
            RESULT=(USB_BUFI[1]==0x18)&(USB_BUFI[2]==0x88)&(USB_BUFI[3]==B);
        }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка OW_WRITE_BYTE" << std::endl; ;
#endif
    return RESULT;
}

bool OW_READ_2BIT(unsigned char * B)
{   //  чтение 2-x бит, 3ms
    bool RESULT=false;
    USB_BUF_CLEAR();
    USB_BUFO[1]=0x18;    USB_BUFO[2]=0x82;
    USB_BUFO[3]=0x01;    USB_BUFO[4]=0x01;
    if (USB_SET_FEATURE())
        {
        usleep(2000);
        if (USB_GET_FEATURE())
            { RESULT=(USB_BUFI[1]==0x18)&(USB_BUFI[2]==0x82); *B=(USB_BUFI[3]&0x01)+((USB_BUFI[4]<<1)&0x02); }
        }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка OW_READ_2BIT" << std::endl; ;
#endif
    return RESULT;
}

bool OW_WRITE_BIT(unsigned char B)
{   //  запись бита, 3ms
    bool RESULT=false;
    USB_BUF_CLEAR();
    USB_BUFO[1]=0x18;    USB_BUFO[2]=0x81;    USB_BUFO[3]=B&0x01;
    if (USB_SET_FEATURE())
        {
        usleep(1000);
        if (USB_GET_FEATURE())
            RESULT=(USB_BUFI[1]==0x18)&(USB_BUFI[2]==0x81)&((USB_BUFI[3]&0x01)==(B&0x01));
        }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка OW_WRITE_BIT" << std::endl; ;
#endif
    return RESULT;
}

bool SEARCH_ROM(unsigned long long ROM_NEXT, int PL)
{   //  поиск ROM, 1 dev - 410ms, 5 dev - 2.26s, 20 dev - 8.89s
    bool RESULT=false;
    unsigned char N=3;
    unsigned char BIT;
    bool CL[64]; for (int i=0; i<64; i++) CL[i]=false;
    unsigned long long RL[64];
    unsigned long long B1=1, CRC, ROM;
    while (!RESULT&((N--)>0))
        {
        ROM=0;
        if (OW_RESET()) RESULT=OW_WRITE_BYTE(0xF0);
        if (RESULT) {
            for (int i=0; i<64; i++) {
                if (RESULT) {
                    if (OW_READ_2BIT(&BIT)) {
                        switch (BIT&0x03)
                            {
                            case 0 :
                                {   //  коллизия есть
                                if (PL<i) {CL[i]=true; RL[i]=ROM;}
                                if (PL>=i) BIT=(ROM_NEXT>>i)&0x01; else BIT=0;
                                if(!OW_WRITE_BIT(BIT)) { RESULT=false; i=64; }
                                if (BIT==1) ROM=ROM+(B1<<i);
                                break;
                                }
                            case 1 : { if (!OW_WRITE_BIT(0x01)) { RESULT=false; i=64; } else ROM=ROM+(B1<<i); break;}
                            case 2 : { if (!OW_WRITE_BIT(0x00)) { RESULT=false; i=64; } break;}
                            case 3 : { RESULT=false; i=64; break;}   //  нет на линии
                            }
                    }
                    else { RESULT=false; i=64; }
                }
            }
        }
        if (ROM==0) RESULT=false;
        if (RESULT) { CRC=0; for (int j=0; j<8; j++) CRC=CRC8(CRC, (ROM>>(j*8))&0xFF); RESULT=CRC==0; }
        }
    if (!RESULT)
    {
#ifdef DEBUG
//    std::cout << "Ошибка SEARCH_ROM" << std::endl;
#endif
    }
        else ONEWIRE_ROM[ONEWIRE_COUNT++]=ROM;
    //  рекурентный вызов поиска
    for (int i=0; i<64; i++)
        if (CL[i]) SEARCH_ROM(RL[i]|(B1<<i), i);
    return RESULT;
}

bool OW_READ_BYTE(unsigned char * B)
{   //  чтение байта, 3ms
    bool RESULT=false;
    USB_BUF_CLEAR();
    USB_BUFO[1]=0x18;    USB_BUFO[2]=0x88;    USB_BUFO[3]=0xFF;
    if (USB_SET_FEATURE())
        {
        usleep(5000);
        if (USB_GET_FEATURE())
            { RESULT=(USB_BUFI[1]==0x18)&(USB_BUFI[2]==0x88); *B=USB_BUFI[3]; }
        }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка OW_READ_BYTE" << std::endl; ;
#endif
    return RESULT;
}

bool OW_READ_4BYTE(unsigned long * B)
{   //  чтение 4 байта, 4ms
    bool RESULT=false;
    USB_BUF_CLEAR();
    USB_BUFO[1]=0x18;    USB_BUFO[2]=0x84;    USB_BUFO[3]=0xFF;
    USB_BUFO[4]=0xFF;    USB_BUFO[5]=0xFF;    USB_BUFO[6]=0xFF;
    if (USB_SET_FEATURE())
        {
        usleep(30000);
        if (USB_GET_FEATURE())
            { RESULT=(USB_BUFI[1]==0x18)&(USB_BUFI[2]==0x84); *B=USB_BUFI[3]+(USB_BUFI[4]<<8)+(USB_BUFI[5]<<16)+(USB_BUFI[6]<<24); }
        }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка OW_READ_4BYTE" << std::endl; ;
#endif
    return RESULT;
}

bool OW_WRITE_4BYTE(unsigned long B)
{   //  запись 4 байта, 4ms
    bool RESULT=false;
    unsigned char D0, D1, D2, D3;
    D0=B&0xFF;
    D1=(B>>8) &0xFF;
    D2=(B>>16)&0xFF;
    D3=(B>>24)&0xFF;
    USB_BUF_CLEAR();
    USB_BUFO[1]=0x18;    USB_BUFO[2]=0x84;    USB_BUFO[3]=B&0xFF;
    USB_BUFO[4]=(B>>8)&0xFF;
    USB_BUFO[5]=(B>>16)&0xFF;
    USB_BUFO[6]=(B>>24)&0xFF;
    if (USB_SET_FEATURE())
        {
        usleep(30000);
        if (USB_GET_FEATURE())
            RESULT=(USB_BUFI[1]==0x18)&(USB_BUFI[2]==0x84)&(USB_BUFI[3]==D0)&(USB_BUFI[4]==D1)&(USB_BUFI[5]==D2)&(USB_BUFI[6]==D3);
        }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка OW_WRITE_4BYTE" << std::endl; ;
#endif
    return RESULT;
}

bool MATCH_ROM(unsigned long long ROM)
{   //  выбор прибора по ROM, 14ms
    bool RESULT=false;
    unsigned long long T=ROM;
    unsigned char N=3;
    while (!RESULT&((N--)>0))
        if (OW_RESET())
            if (OW_WRITE_BYTE(0x55))
                if (OW_WRITE_4BYTE(T&0xFFFFFFFF))
                    RESULT=OW_WRITE_4BYTE((T>>32)&0xFFFFFFFF);
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка MATCH_ROM" << std::endl;
#endif
    return RESULT;
}

bool SKIP_ROM_CONVERT()
{   //  пропуск ROM-команд, старт измерения температуры, 9ms
    bool RESULT=false;
    unsigned char N=3;
    while (!RESULT&((N--)>0))
        if (OW_RESET())
            if (OW_WRITE_BYTE(0xCC))
                RESULT=OW_WRITE_BYTE(0x44);
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка SKIP_ROM_CONVERT" << std::endl;
#endif
    return RESULT;
}

bool GET_TEMPERATURE(unsigned long long ROM, float * T)
{   //  чтение температуры, 28ms
    unsigned long long CRC;
    unsigned long L1, L2;
    unsigned char L3;
    unsigned char FAMILY=ROM&0xFF;
    bool RESULT=false;
    unsigned char N=3;
    while ((!RESULT)&&((N--)>0))
        if (MATCH_ROM(ROM))
            if (OW_WRITE_BYTE(0xBE))
                    if (OW_READ_4BYTE(&L1))
                        if (OW_READ_4BYTE(&L2))
                            if (OW_READ_BYTE(&L3))
                                {
                                CRC=0;
                                for (int i=0; i<4; i++) CRC=CRC8(CRC, (L1>>(i*8))&0xFF);
                                for (int i=0; i<4; i++) CRC=CRC8(CRC, (L2>>(i*8))&0xFF);
                                CRC=CRC8(CRC, L3);
                                RESULT=CRC==0;
                                short K=L1&0xFFFF;
                                //  DS18B20 +10.125=00A2h, -10.125=FF5Eh
                                //  DS18S20 +25.0=0032h, -25.0=FFCEh
                                //  K=0x0032;
                                *T=1000;     //  для неопознанной FAMILY датчик отсутствует
                                if ((FAMILY==0x28)|(FAMILY==0x22)) *T=K*0.0625;  //  DS18B20 | DS1822
                                if (FAMILY==0x10) *T=K*0.5;                      //  DS18S20 | DS1820
                                }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка GET_TEMPERATURE" << std::endl;
#endif
    return RESULT;
}

bool READ_TEMPERATURE(void)
{
    bool RESULT = true;

//    SEARCH_ROM(0, 0);
    if (ONEWIRE_COUNT <= 0) { printw("Датчики не найдены\n"); RESULT = false; }
    else
    {
        if (!SKIP_ROM_CONVERT()) { printw("Ошибка SKIP_ROM_CONVERT\n"); RESULT = false; }
        else
        {
             float T; // Измеренная температура
             for (int i=0; i<ONEWIRE_COUNT; i++)
                 if (GET_TEMPERATURE(ONEWIRE_ROM[i], &T))
                 {
	             printw("ROM=%lx T=%g\n", ONEWIRE_ROM[i], T);
                 }
        }
    }
    return RESULT;
}

//##########################################################################################
bool USB_EE_RD(unsigned char ADR, unsigned char * DATA)
{   //  чтение EEPROM
    USB_BUF_CLEAR();
    bool RESULT=false;
    USB_BUFO[1]=0xE0;
    USB_BUFO[2]=ADR;
    int i=3;   //  число попыток
    while (!RESULT & ((i--)>0))
        if (USB_SET_FEATURE())
            if (USB_GET_FEATURE()) { RESULT=(USB_BUFI[1]==0xE0)&(USB_BUFI[2]==ADR); *DATA=USB_BUFI[3]; }
#ifdef DEBUG
    if (!RESULT) std::cout << "Ошибка чтения EEPROM" << std::endl;
#endif
    return RESULT;
}

void TERMOSTATE_READ()
{   //  чтение уставок термостата
    unsigned char H,L; short S;
    if (USB_EE_RD(0x10, &H)&&USB_EE_RD(0x11, &L)) { S=(H<<8)+L; printw("T1H: %d\n", S/16); }
        else printw("T1H: ERR\n");
    if (USB_EE_RD(0x12, &H)&&USB_EE_RD(0x13, &L)) { S=(H<<8)+L; printw("T1L: %d\n", S/16); }
        else printw("T1L: ERR\n");
    if (USB_EE_RD(0x14, &H)&&USB_EE_RD(0x15, &L)) { S=(H<<8)+L; printw("T2H: %d\n", S/16); }
        else printw("T2H: ERR\n");
    if (USB_EE_RD(0x16, &H)&&USB_EE_RD(0x17, &L)) { S=(H<<8)+L; printw("T2L: %d\n", S/16); }
        else printw("T2L: ERR\n");
}


int main()
{
    int res; // переменная для возвращаемых значений
    wchar_t wstr[MAX_STR];

    setlocale(LC_ALL, "");
    
    WINDOW * win;
    
//    signal(SIGINT, sig);

    if (res = hid_init()){ // инициализировать библиотеку hidapi
	fprintf(stderr,"Ошибка инициализации hidapi, код: %d.\n", res);
	return 1;
    }

    if (!initscr())	// Переход в curses-режим
	fprintf(stderr, "Проблема инициализации ncurses\n");
//    raw(); // полный контроль над клавиатурой
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLUE);
    bkgd(COLOR_PAIR(1));

    printw("Thermus v.0.1\n");  // Отображение приветствия в буфер

// определить присутствие устройства и получить его дескриптор
    handle = hid_open(0x16c0, 0x05df, NULL);
    if (handle)
    {
// вывести описание устройства
    // Read the Manufacturer String
    res = hid_get_manufacturer_string(handle, wstr, MAX_STR);
    printw("Manufacturer String: %ls\n", wstr);

    // Read the Product String
    res = hid_get_product_string(handle, wstr, MAX_STR);
    printw("Product String: %ls\n", wstr);

    if (USB_GET_ID(&Device_ID)) printw("ID: %d\n", Device_ID);

    SEARCH_ROM(0LL, 0);
    TERMOSTATE_READ();
    printw("Датчиков: %d\n",ONEWIRE_COUNT);
    READ_TEMPERATURE();
// завершить работу с устройством
    hid_close(handle);
    }

// здесь надо прочитать состояние термостата и вывести на панель

do
{
    refresh();                   // Вывод данных на настоящий экран

    win = newwin(LINES/2, COLS/2, 0, COLS/2);
    wbkgd(win,COLOR_PAIR(1));
    wmove(win, 1, 1); wprintw(win,"Датчиков: %d\n",ONEWIRE_COUNT);
    box(win, 0, 0);
    wrefresh(win);

} while (getch()==KEY_RESIZE);   // Ожидание нажатия какой-либо клавиши пользователем

    delwin(win);

    endwin();                    // Выход из curses-режима. Обязательная команда.
    res = hid_exit(); // завершить работу с библиотекой hidapi
    return res;
}
